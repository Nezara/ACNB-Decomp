/* src/menu/mmd.cpp - the menu's model-data (MMD) manager (OLMENU): 256 stMmdData slots (0x60 bytes) in a menu-heap
 * block, each holding one loaded model package; a 48-entry FIFO of async CD reads (struct_e9ef92fc, 0x90 bytes) that
 * loads part models through the loader one at a time (polled from ReadSync), and the "general" models of the menu
 * archive MenuMng.dataTbl[1] (3 object models kind 1, 3 kind-2 models).
 * SetData copies a package (MDATA archive: header, OMD / TM2 / MI2 / OTH chunks and their prm chunks) into one menu-heap
 * block, relocates it, pre-builds the GS load-image packets of every TM2 texture + CLUT, records the "data polygons"
 * (marker triangles whose vertex colour is an attach-point id; recorded and made transparent) and shifts the TBPs of
 * part models (kind 1) into the menu's model texture area.
 * The CD / loader callees of __ReadProc and ReadStart are interpreted: their call order is kept exactly (CD timing). */

#include <string.h>
#include "acnb/sdk.h"

/* The release tail shared by DeleteMMD, DeleteMMDByKind and ReleaseGeneralMMD: only decrements the slot high-water
 * mark by one, and only when the freed slot is at or above it (re-reads both globals). */
static inline void mmd_release_slot(stMmdData *p)
{
    if (p->datap) {
        DF_MMM_Free(0, p->datap);
        p->datap = 0;
    }
    int idx = (int)(p - MMDTopp);
    int max = MMDCurMaxIdx;
    if (!(idx + 1 < max)) MMDCurMaxIdx = max - 1;
    p->f_use = 0;
}

/* The GetMMD search, inlined by the original in GetMMD / SetGeneralMMD / ReleaseGeneralMMD. */
static inline stMmdData *mmd_find(int kind, int id)
{
    int max = MMDCurMaxIdx;
    stMmdData *p = MMDTopp;
    int i = 0;
    for (; i < max; i++, p++)
        if (p->f_use == 1 && p->kind == kind && p->id == id) break;
    return (i < max) ? p : 0;
}

/* ring step of the read queue: wraps at entry 48 */
static inline struct_e9ef92fc *mmd_read_next(struct_e9ef92fc *p, struct_e9ef92fc *top)
{
    p++;
    if (!((int)(p - top) < 0x30)) p = top;
    return p;
}

void *DF_MMD_GetObjectMMD(int inObjID)
{
    PS2_FRAME(0x60);
    void *arc = MenuMng.dataTbl[1];
    struct_add4cfba *a = (struct_add4cfba *)arc;
    for (unsigned int i = 0; i < a->num; i++) {   /* num re-read every pass */
        void *e = DF_MDATA_GetData(arc, (int)i);
        if (!e) continue;
        stMmdHead *h = (stMmdHead *)DF_MDATA_GetData(e, 0);
        if (h && (int)h->id == inObjID) return e;
    }
    return 0;
}

void DF_MMD_ReleaseGeneralMMD(void)
{
    PS2_FRAME(0x50);
    static const int tbl[3] = {0xa0, 0xa1, 0xa2};   /* OLMENU 0x1edf030, copied to sp+0x40 */
    for (int i = 0; i < 3; i++) {
        stMmdData *p = mmd_find(1, 0x3a99 + i);
        if (p && p->f_use) mmd_release_slot(p);
    }
    for (unsigned int i = 0; i < 3; i++) {
        stMmdData *p = mmd_find(2, tbl[i]);
        if (p && p->f_use) mmd_release_slot(p);
    }
}

/* No null tests: a full slot table or an id missing from the archive crashes in SetData (original too). */
void DF_MMD_SetGeneralMMD(void)
{
    PS2_FRAME(0x60);
    static const int tbl[3] = {0xa0, 0xa1, 0xa2};   /* OLMENU 0x1edf020, copied to sp+0x50 */
    for (int i = 0; i < 3; i++) {
        int id = 0x3a99 + i;
        if (!mmd_find(1, id)) {
            stMmdData *m = DF_MMD_GetNewMMD(0);
            void *d = DF_MMD_GetObjectMMD(5 + i);
            DF_MMD_SetData(m, 1, id, d);
        }
    }
    for (unsigned int i = 0; i < 3; i++) {
        int id = tbl[i];
        if (!mmd_find(2, id)) {
            stMmdData *m = DF_MMD_GetNewMMD(0);
            void *d = DF_MMD_GetObjectMMD(id);
            DF_MMD_SetData(m, 2, id, d);
        }
    }
}

/* Returns the matching data polygon's object index and its centre / normal (w = 1.0); -1 on a miss (outputs
 * untouched). Stops at the first record with f_on == 0: records after the last marker found by __SetDataPoly are
 * stale heap data (the buffer is never cleared). */
int DF_MMD_GetDataPoly(stMmdData *inMmdp, int inNum, int inID, float *outVert, float *outNorm)
{
    if (!inMmdp) return -1;
    stMmdDp *dp = inMmdp->OMD.datapolyp;
    if (!dp) return -1;
    for (int i = 0; i < 0x14; i++, dp++) {
        if (dp->f_on == 0) return -1;
        if (dp->omd_num == inNum && dp->id == inID) {
            outVert[0] = dp->vx;
            outVert[1] = dp->vy;
            outVert[2] = dp->vz;
            outVert[3] = 1.0f;
            outNorm[0] = dp->nx;
            outNorm[1] = dp->ny;
            outNorm[2] = dp->nz;
            outNorm[3] = 1.0f;
            return dp->obj_id;
        }
    }
    return -1;
}

/* Cancels the queued read of inMmdp. A read in flight (step 1) only drops the loader / read-buffer flags: the CD
 * transfer itself goes on. The Readp2 entry (never filled) is tested without its f_use flag. */
int DF_MMD_ReadBreak(stMmdData *inMmdp)
{
    if (!inMmdp) return -1;
    int ret = -1;
    struct_e9ef92fc *p = MMDReadCurp, *top = MMDReadTopp;
    for (int i = 0; i < 0x30; i++) {
        if (p->f_use && p->step != 0x63 && p->mmdp == inMmdp) {
            if (p->step == 1) {
                MMDIsUseCdvd = 0;
                MenuMng.f_use_readbuff = 0;
            }
            p->f_end = 1;
            p->f_use = 0;
            p->step = 0x63;
            p->mmdp = 0;
            ret = 1;
            break;
        }
        p = mmd_read_next(p, top);
    }
    struct_e9ef92fc *r2 = MMDReadp2;
    if (r2->mmdp == inMmdp) {
        if (r2->step == 1) {
            MMDIsUseCdvd = 0;
            MenuMng.f_use_readbuff = 0;
        }
        r2->f_end = 1;
        r2->f_use = 0;
        r2->step = 0x63;
        r2->mmdp = 0;
        ret = 1;
    }
    return ret;
}

/* Polls the read queue from its head. Mode 0: 1 when every queued read is done (or the queue is empty), 0 while the
 * head is in flight, -1 / -2 on errors; ReadProc results other than -2 / -1 / 0 leave res as it was. Any other mode
 * stops only on -1, and mode 1 restarts the scan while res is 0 (blocking). */
int DF_MMD_ReadSync(int inMode)
{
    PS2_FRAME(0x60);
    int res, again;
    struct_e9ef92fc *cur;
    do {
        cur = MMDReadCurp;
        again = 0;
        res = 1;
        for (int i = 0; i < 0x30; i++) {
            if (cur->f_use) {
                int r = __ReadProc(cur);
                if (r == -2) res = -2;
                else if (r == -1) res = -1;
                else if (r == 1) {}
                else if (r == 0) res = 0;
                if (inMode == 0) {
                    if (res != 1) break;
                } else if (res == -1) break;
            }
            if (MMDReadp2->f_use == 1 && res == 1) {   /* dead: Readp2 is never filled */
                res = 0;
                break;
            }
            cur = mmd_read_next(cur, MMDReadTopp);
        }
        MMDReadCurp = cur;
        if (res == 0 && inMode == 1) again = 1;
    } while (again == 1);
    return res;
}

/* One step of a queued read: 0 = issue the async read when CD, loader and read buffer are free; 1 = when the loader
 * is done, decompress into g_pbufRead + 0x7ffc0 and SetData (unless the slot got data meanwhile); 0x63 = done.
 * A negative async result is returned with the flags left set (the entry retries; never reached). */
static int __ReadProc(struct_e9ef92fc *ioRmp)
{
    PS2_FRAME(0x30);
    int ret = 0;
    switch (ioRmp->step) {
    case 0x63:
        return 1;
    case 1: {
        if (loader_IsAsyncBusy()) return 0;
        int r = loader_GetAsyncResult();
        if (r < 0) return r;
        if (ioRmp->mmdp->datap == 0) {
            char *dst = g_pbufRead + 0x7ffc0;
            Gfz_Uncompress((StGfzHeader *)g_pbufRead, dst);
            DF_MMD_SetData(ioRmp->mmdp, ioRmp->kind, ioRmp->id, dst);
        }
        ioRmp->f_end = 1;
        ioRmp->f_use = 0;
        ioRmp->step = 0x63;
        ioRmp->mmdp = 0;
        MMDIsUseCdvd = 0;
        MenuMng.f_use_readbuff = 0;
        return 1;
    }
    case 0:
        /* Cdvd_CdSync has no DWARF parameter but the original passes a0 = 0 */
        if (gcall_as<int(int)>(GFN(Cdvd_CdSync), 0) == 1) return 0;
        if (loader_IsAsyncBusy()) return 0;
        if (MMDIsUseCdvd == 1) return 0;
        if (MenuMng.f_use_readbuff == 1) return 0;
        loader_ReadMmdAsync(g_pbufRead, ioRmp->path, ioRmp->lsn_id);
        ioRmp->step = 1;
        MMDIsUseCdvd = 1;
        MenuMng.f_use_readbuff = 1;
        return 0;
    }
    return ret;
}

/* Queues a read of model inID for ioMmdp: -1 null slot, 0 already queued, -2 queue full, 1 queued. Callers always
 * pass inModefTruId = 0x10000 (default case). The loader's 5th argument is whatever the switch left in v0
 * (Index2TrueID's result in the default case). Kinds other than 1 leave the entry's lsn / path stale. */
int DF_MMD_ReadStart(stMmdData *ioMmdp, int inKind, int inID, int inModefTruId)
{
    PS2_FRAME(0x90);
    int v0 = 0, s0 = 0, s1 = 0, s7 = 0;
    if (!ioMmdp) return -1;
    int s6 = inID / 1000;
    int rem = inID % 1000;
    /* original: if (rem == 0 && (inModefTruId & 0xff0000) == 1) return 0; - dead, the masked value is never 1 */
    if (inKind == 1) {
        s7 = (int)((unsigned int)(inModefTruId & 0xff000000) >> 24);
        switch (inModefTruId & 0xff0000) {
        case 0x40000: s0 = 1; v0 = inModefTruId & 0xffff; s1 = 3; break;
        case 0x30000: v0 = inModefTruId & 0xffff; s1 = 2; s0 = 0; break;
        case 0x20000: s0 = 1; v0 = inModefTruId & 0xffff; s1 = 2; break;
        default:
            s1 = 1;
            s0 = 1;
            v0 = DF_MPART_Index2TrueID(s6, rem);
            break;
        }
    }
    struct_e9ef92fc *p = MMDReadCurp, *top = MMDReadTopp;
    int i = 0;
    for (; i < 0x30; i++) {
        if (!p->f_use) break;
        if (p->kind == inKind && p->id == inID) return 0;
        p = mmd_read_next(p, top);
    }
    if (!(i < 0x30)) return -2;
    if (inKind == 1)
        p->lsn_id = loader_GetMmdLocation(p->path, inID, s7, s6, v0, s1, s0);
    p->mmdp = ioMmdp;
    p->kind = (char)inKind;
    p->id = inID;
    p->f_end = 0;
    p->f_use = 1;
    p->step = 0;
    return 1;
}

/* DMA call tags for the texture / CLUT load packets of every texture whose flag byte is not 1, then TEXFLUSH. */
unsigned int *DF_MMD_MakeTM2Packet(unsigned int *ioDmap, stMmdData *inMmdp, unsigned char *inFlagp)
{
    PS2_FRAME(0x50);
    if (inMmdp->TM2.num == 0 || !inMmdp->f_use) return ioDmap;
    stMmdTm2Data *e = inMmdp->TM2.tm2datap;
    for (int i = 0; i < inMmdp->TM2.num; i++, e++) {
        if (*inFlagp++ == 1) continue;
        if (e->tex_img.datap) ioDmap = DF_MPKT_MakeCall(ioDmap, e->tex_img.datap);
        if (e->clut_img.datap) ioDmap = DF_MPKT_MakeCall(ioDmap, e->clut_img.datap);
    }
    return DF_MPKT_MakeGSRegister(ioDmap, 0x3f, 0xffffffffULL);
}

void DF_MMD_DeleteMMD(stMmdData *ioMmdp)
{
    PS2_FRAME(0x20);
    if (ioMmdp && ioMmdp->f_use) mmd_release_slot(ioMmdp);
}

/* end is inclusive: slot index MMDCurMaxIdx is examined too. */
void DF_MMD_DeleteMMDByKind(int inKind)
{
    PS2_FRAME(0x40);
    stMmdData *p = MMDTopp;
    stMmdData *end = p + MMDCurMaxIdx;
    if ((uintptr_t)end < (uintptr_t)p) return;
    do {
        if (p->f_use && p->kind == inKind && p && p->f_use) mmd_release_slot(p);
        p++;
    } while (!((uintptr_t)end < (uintptr_t)p));
}

/* The scan runs to Topp + 0x6000 inclusive: with every slot in use it would claim read-queue entry 0 (clearing 0x60
 * bytes, raising MMDCurMaxIdx to 0x101) and still return 0. */
stMmdData *DF_MMD_GetNewMMD(stMmdData *inMmdp)
{
    PS2_FRAME(0x30);
    stMmdData *p = inMmdp ? inMmdp : MMDTopp;
    stMmdData *end = (stMmdData *)((char *)MMDTopp + 0x6000);
    if (!((uintptr_t)end < (uintptr_t)p)) {
        do {
            if (p->f_use == 0) {
                DF_MFUNC_ClearMemory(p, 0x18);
                p->f_use = 1;
                int idx = (int)(p - MMDTopp) + 1;
                if (MMDCurMaxIdx < idx) MMDCurMaxIdx = idx;
                break;
            }
            p++;
        } while (!((uintptr_t)end < (uintptr_t)p));
    }
    return ((uintptr_t)p < (uintptr_t)end) ? p : 0;
}

stMmdData *DF_MMD_GetMMD(int inKind, int inID)
{
    return mmd_find(inKind, inID);
}

/* Layout of ioBuff (ClearMemory sizes are words): slots, read queue, Readp2, PushSysp. */
unsigned char *DF_MMD_Init(unsigned char *ioBuff)
{
    PS2_FRAME(0x20);
    unsigned char *s0 = (unsigned char *)DF_MFUNC_Aligned(ioBuff, 0x10);
    MMDTopp = (stMmdData *)s0;
    DF_MFUNC_ClearMemory(MMDTopp, 0x1800);
    s0 = (unsigned char *)DF_MFUNC_Aligned(s0 + 0x6000, 0x10);
    MMDReadTopp = (struct_e9ef92fc *)s0;
    MMDReadCurp = (struct_e9ef92fc *)s0;
    DF_MFUNC_ClearMemory(MMDReadTopp, 0x6c0);
    MMDReadp2 = (struct_e9ef92fc *)(s0 + 0x1b00);
    DF_MFUNC_ClearMemory(MMDReadp2, 0x24);
    MMDPushSysp = (struct_24cd3bfc *)(s0 + 0x1b90);
    MMDIsUseCdvd = 0;
    MMDCurMaxIdx = 0;
    DF_MFUNC_ClearMemory(MMDPushSysp, 0x401);
    return s0 + 0x2b94;
}

/* ---- SetData and its statics ---- */

/* TEX0 of a TM2 picture header (64-bit word at +0x10): tbp0 0..13, psm 20..25, tw 26..29, th 30..33, cbp 37..50,
 * cpsm 51..54, csm 55. */
static inline unsigned long long mmd_tex0(const unsigned char *t)
{
    unsigned long long v;
    memcpy(&v, t + 0x10, 8);
    return v;
}

/* CLUT upload size for the indexed formats (0 = no CLUT): 4-bit 8 x 2 (CSM1) / 16 x 1 (CSM2), 8-bit 16 x 16 / 256 x 1
 * (dbw 4); dbw is the buffer width CalcTM2BuffSize passes (MakeLoadImage derives the same from w). */
static inline int mmd_clut_dims(unsigned long long tex0, int *w, int *h, int *dbw)
{
    unsigned int psm = (unsigned int)(tex0 >> 20) & 0x3f;
    int csm = (int)(tex0 >> 55) & 1;
    if (psm == 0x14) {
        *dbw = 1;
        if (!csm) { *w = 8; *h = 2; } else { *w = 0x10; *h = 1; }
        return 1;
    }
    if (psm == 0x1b || psm == 0x13) {
        if (!csm) { *dbw = 1; *w = 0x10; *h = 0x10; } else { *dbw = 4; *w = 0x100; *h = 1; }
        return 1;
    }
    return 0;
}

/* nloop / image / size of a load packet built at img->datap and ending at inEnd */
static inline void mmd_set_img(stMmdImage *img, unsigned char *inEnd)
{
    unsigned char *d = (unsigned char *)img->datap;
    img->nloop = *(unsigned short *)(d + 0x70) & 0x7fff;
    img->imagep = (unsigned int *)(d + 0x80);
    img->size = (unsigned int)((int)(inEnd - d) / 4 * 4);
}

/* Shifts every TBP0 in 0x1a40..0x22ff by 0x1180 (part models' textures live above the menu's own). */
static void __MMD_OffsetTBP(stMmdData *ioMmdp, int inOmdNum)
{
    PS2_FRAME(0x30);
    (void)inOmdNum;
    if (!ioMmdp) return;
    for (int i = 0; i < (signed char)ioMmdp->OMD.num; i++) {
        struct_38528994 *h = (struct_38528994 *)DF_MDATA_GetData(ioMmdp->OMD.datap, i);
        unsigned char *rec = (unsigned char *)h + 0x10;
        for (unsigned int j = 0; j < h->nobj; j++, rec += 0x10) {
            unsigned int groups = *(unsigned short *)(rec + 0xa);
            unsigned char *data = (unsigned char *)(uintptr_t)*(unsigned int *)(rec + 4);
            while (groups--) {
                unsigned int cnt = *(unsigned int *)(data + 0x80) & 0x7fff;
                unsigned short tbp = *(unsigned short *)(data + 0x40);
                int t = tbp & 0x3fff;
                if (t >= 0x1a40 && t < 0x2300)
                    *(unsigned short *)(data + 0x40) = (unsigned short)((tbp & 0xc000) | ((t + 0x1180) & 0x3fff));
                data += 0xa0 + cnt * 0x40 + 0x10;
            }
        }
    }
}

/* Copies the OMD chunk, relocates each object record's data pointer, reserves the 0x280-byte data-poly buffer (not
 * cleared), copies the prm chunk. */
static unsigned char *__RemapOMD(stMmdOmd *ioOMDp, unsigned char *ioBuff, struct_add4cfba *inDatap,
                                 struct_add4cfba *inPrmp, stMmdOmdInfo *inInfop)
{
    PS2_FRAME(0x60);
    unsigned int sz = inDatap->size;
    ioOMDp->datap = (struct_add4cfba *)ioBuff;
    DF_MFUNC_CopyMemory(ioBuff, inDatap, (int)sz >> 2);
    ioOMDp->num = (char)ioOMDp->datap->num;
    DF_MDATA_Remap(ioOMDp->datap);
    ioBuff += sz;
    for (int i = 0; i < (signed char)ioOMDp->num; i++) {
        struct_38528994 *h = (struct_38528994 *)DF_MDATA_GetData(ioOMDp->datap, i);
        if (!h) continue;
        unsigned char *rec = (unsigned char *)h + 0x10;
        for (unsigned int j = 0; j < h->nobj; j++, rec += 0x10) *(unsigned int *)(rec + 4) += PS2PTR(h);
    }
    ioOMDp->datapolyp = (stMmdDp *)ioBuff;
    ioBuff += 0x280;
    sz = inPrmp->size;
    DF_MFUNC_CopyMemory(ioBuff, inPrmp, (int)sz >> 2);
    ioOMDp->prmp = (struct_add4cfba *)ioBuff;
    DF_MDATA_Remap(ioOMDp->prmp);
    ioBuff += sz;
    ioOMDp->infop = inInfop;
    return ioBuff;
}

/* One stMmdTm2Data per texture, then the load packets (MakeLoadImage + MakeEnd) of the texture and, for 4 / 8-bit
 * formats, its CLUT. kind 1 shifts the texture's TBP by 0x1180 (not the CLUT's CBP). */
static unsigned char *__RemapTM2(stMmdTm2 *ioTM2p, unsigned char *ioBuff, struct_add4cfba *inDatap,
                                 struct_add4cfba *inPrmp, int kind)
{
    PS2_FRAME(0x90);
    unsigned int off = (kind == 1) ? 0x1180 : 0;
    ioTM2p->num = (int)inDatap->num;
    ioTM2p->tm2datap = (stMmdTm2Data *)ioBuff;
    ioBuff += ioTM2p->num << 6;
    DF_MFUNC_ClearMemory(ioTM2p->tm2datap, (ioTM2p->num << 6) >> 2);
    stMmdTm2Data *e = ioTM2p->tm2datap;
    for (int i = 0; i < ioTM2p->num; i++, e++) {
        unsigned char *t = (unsigned char *)DF_MDATA_GetData(inDatap, i);
        if (!t) continue;
        memcpy(&e->tex, t + 0x10, 0x10);
        unsigned long long tex0 = mmd_tex0(t);
        e->tex_img.datap = (unsigned int *)ioBuff;
        unsigned int *q = DF_MPKT_MakeLoadImage((unsigned int *)ioBuff, t + 0x20, ((unsigned int)tex0 & 0x3fff) + off,
                                                (unsigned int)(tex0 >> 20) & 0x3f, 0, 0, 1 << ((tex0 >> 26) & 0xf),
                                                1 << ((tex0 >> 30) & 0xf));
        ioBuff = (unsigned char *)DF_MPKT_MakeEnd(q);
        mmd_set_img(&e->tex_img, ioBuff);
        int cw, ch, dbw;
        if (!mmd_clut_dims(tex0, &cw, &ch, &dbw)) continue;
        unsigned char *c = t + *(unsigned int *)(t + 4) + 0x20;
        e->clut_img.datap = (unsigned int *)ioBuff;
        q = DF_MPKT_MakeLoadImage((unsigned int *)ioBuff, c, (unsigned int)(tex0 >> 37) & 0x3fff,
                                  (unsigned int)(tex0 >> 51) & 0xf, 0, 0, cw, ch);
        ioBuff = (unsigned char *)DF_MPKT_MakeEnd(q);
        mmd_set_img(&e->clut_img, ioBuff);
    }
    unsigned int sz = inPrmp->size;
    DF_MFUNC_CopyMemory(ioBuff, inPrmp, (int)sz >> 2);
    ioTM2p->prmp = (struct_add4cfba *)ioBuff;
    DF_MDATA_Remap(ioTM2p->prmp);
    return ioBuff + sz;
}

static unsigned char *__RemapMI2(stMmdMi2 *ioMI2p, unsigned char *ioBuff, struct_add4cfba *inDatap,
                                 struct_add4cfba *inPrmp)
{
    PS2_FRAME(0x50);
    unsigned int sz = inDatap->size;
    DF_MFUNC_CopyMemory(ioBuff, inDatap, (int)sz >> 2);
    ioMI2p->datap = (struct_add4cfba *)ioBuff;
    ioMI2p->num = (char)ioMI2p->datap->num;
    DF_MDATA_Remap(ioMI2p->datap);
    ioBuff += sz;
    for (int i = 0; i < (signed char)ioMI2p->num; i++) DF_MFUNC_RemapMI2(DF_MDATA_GetData(ioMI2p->datap, i));
    sz = inPrmp->size;
    DF_MFUNC_CopyMemory(ioBuff, inPrmp, (int)sz >> 2);
    ioMI2p->prmp = (struct_add4cfba *)ioBuff;
    DF_MDATA_Remap(ioMI2p->prmp);
    return ioBuff + sz;
}

/* Bytes RemapTM2 needs: 0x40 per texture plus each load packet (NLOOP quads + 0x90 bytes of tags / GS header), NLOOP
 * taken from the SDK's own load-image header (built on the original stack slot). */
static int __CalcTM2BuffSize(struct_add4cfba *inHeadp)
{
    PS2_FRAME(0xd0);
    PS2_STACK(struct_cb63eb70, li, 0x70);   /* sceGsLoadImage: built by the interpreted SDK */
    const unsigned short *nloop = (const unsigned short *)((unsigned char *)&li + 0x50);   /* giftag1 NLOOP */
    int size = (int)(inHeadp->num << 6);
    for (unsigned int i = 0; i < inHeadp->num; i++) {
        unsigned char *t = (unsigned char *)DF_MDATA_GetData(inHeadp, (int)i);
        if (!t) continue;
        unsigned long long tex0 = mmd_tex0(t);
        int w = 1 << ((tex0 >> 26) & 0xf);
        int h = 1 << ((tex0 >> 30) & 0xf);
        sceGsSetDefLoadImage(&li, (short)(tex0 & 0x3fff), (short)(w / 64 + (w % 64 != 0)), (short)((tex0 >> 20) & 0x3f),
                             0, 0, (short)w, (short)h);
        /* SyncDCache(&li, sp + 0xd0): no-op on the host */
        size += (*nloop & 0x7fff) * 16 + 0x90;
        int cw, ch, dbw;
        if (!mmd_clut_dims(tex0, &cw, &ch, &dbw)) continue;
        sceGsSetDefLoadImage(&li, (short)((tex0 >> 37) & 0x3fff), (short)dbw, (short)((tex0 >> 51) & 0xf), 0, 0,
                             (short)cw, (short)ch);
        /* SyncDCache(&li, sp + 0xd0): no-op on the host */
        size += (*nloop & 0x7fff) * 16 + 0x90;
    }
    return size;
}

/* Finds the first vertex (first of a triangle) with alpha >= 254.9 and colour within +-0.5 of (inR, inG, inB):
 * outV = centre of the triangle, outN = its first normal (w copied), the three alphas are zeroed; returns the object
 * index or -1. */
static int __GetDataPoly(struct_38528994 *inOhp, float inR, float inG, float inB, float *outV, float *outN)
{
    PS2_FRAME(0x40);
    const float half = 0.5f, opaque = 0x1.fdccccp+7f;   /* 254.9f = 0x437ee666 */
    float lo_r = inR - half, hi_r = half + inR;
    float lo_g = inG - half, hi_g = half + inG;
    float lo_b = inB - half, hi_b = half + inB;
    unsigned int nobj = inOhp->nobj;
    unsigned char *rec = (unsigned char *)inOhp + 0x10;
    for (unsigned int o = 0; o < nobj; o++, rec += 0x10) {
        unsigned char *data = (unsigned char *)(uintptr_t)*(unsigned int *)(rec + 4);
        unsigned int groups = *(unsigned short *)(rec + 0xa);
        while (groups--) {
            unsigned int cnt = *(unsigned int *)(data + 0x80) & 0x7fff;
            data += 0xa0;
            for (; cnt; cnt--, data += 0x40) {
                const float *v = (const float *)data;
                if (v[15] < opaque) continue;
                if (v[12] < lo_r || hi_r < v[12]) continue;
                if (v[13] < lo_g || hi_g < v[13]) continue;
                if (v[14] < lo_b || hi_b < v[14]) continue;
                const float *v1 = (const float *)(data + 0x40), *v2 = (const float *)(data + 0x80);
                memcpy(outV, data, 16);
                for (int k = 0; k < 3; k++) outV[k] = outV[k] + v1[k];   /* vadd.xyz, stored */
                for (int k = 0; k < 3; k++) outV[k] = outV[k] + v2[k];
                sceVu0DivVectorXYZ(outV, outV, 3.0f);
                memcpy(outN, data + 0x10, 16);
                *(unsigned int *)(data + 0x3c) = 0;
                *(unsigned int *)(data + 0x7c) = 0;
                *(unsigned int *)(data + 0xbc) = 0;
                return (int)o;
            }
            data += 0x10;
        }
    }
    return -1;
}

/* Records up to 20 data polygons (ids of inIDTablep, -1 terminated, per OMD). Records after the last hit stay as
 * the heap left them. */
static void __SetDataPoly(stMmdOmd *ioOMDp, int *inIDTablep)
{
    PS2_FRAME(0xd0);
    float outV[4] __attribute__((aligned(16)));   /* sp+0xc0, fully written on a hit */
    float outN[4] __attribute__((aligned(16)));   /* sp+0xb0 */
    int n = 0, off = 0;
    for (int i = 0; i < (signed char)ioOMDp->num; i++) {
        struct_38528994 *h = (struct_38528994 *)DF_MDATA_GetData(ioOMDp->datap, i);
        for (int *idp = inIDTablep; *idp != -1; idp++) {
            stMmdDp *e = (stMmdDp *)((unsigned char *)ioOMDp->datapolyp + off);
            int obj = __GetDataPoly(h, (float)*idp, 0.0f, 0.0f, outV, outN);
            if (obj < 0) continue;
            n++;
            e->f_on = 1;
            e->omd_num = (unsigned char)i;
            off += 0x20;
            e->id = (unsigned char)*idp;
            e->obj_id = (unsigned char)obj;
            e->vx = outV[0];
            e->vy = outV[1];
            e->vz = outV[2];
            e->nx = outN[0];
            e->ny = outN[1];
            e->nz = outN[2];
            if (n >= 0x14) return;
        }
    }
}

/* Zeroes the alphas of the first opaque (alpha >= 254.9) vertex triple and returns. */
static void __SetPloyAlpha255To0(struct_38528994 *inOhp)
{
    const float opaque = 0x1.fdccccp+7f;   /* 254.9f */
    unsigned int nobj = inOhp->nobj;
    unsigned char *rec = (unsigned char *)inOhp + 0x10;
    for (unsigned int o = 0; o < nobj; o++, rec += 0x10) {
        unsigned char *data = (unsigned char *)(uintptr_t)*(unsigned int *)(rec + 4);
        unsigned int groups = *(unsigned short *)(rec + 0xa);
        while (groups--) {
            unsigned int cnt = *(unsigned int *)(data + 0x80) & 0x7fff;
            data += 0xa0;
            for (; cnt; cnt--, data += 0x40) {
                if (*(const float *)(data + 0x3c) < opaque) continue;
                *(unsigned int *)(data + 0x3c) = 0;
                *(unsigned int *)(data + 0x7c) = 0;
                *(unsigned int *)(data + 0xbc) = 0;
                return;
            }
            data += 0x10;
        }
    }
}

/* Data-poly id lists per id / 1000 (jump table OLMENU 0x1ee5c90; 14 and anything outside 0..15 = ids 0..0x7f). */
static const int mmd_dp_k0[] = {0x45, 0x46};
static const int mmd_dp_k1[] = {0, 1, 2, 3, 0x3d, 0x50, 0x5a, 0x5b, 0x5c, 0x5d, 0x64, 0x65};
static const int mmd_dp_k2[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 0x16, 0x28, 0x29, 0x2a, 0x2b, 0x3e,
                                0x51, 0x52};
static const int mmd_dp_k3[] = {0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x4a, 0x4b, 0x4c, 0x4d};
static const int mmd_dp_k4[] = {0x48, 0x49};
static const int mmd_dp_k8[] = {0xc, 0xd, 0xe, 0xf, 0x10, 0x16};
static const int mmd_dp_k15[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

/* Records the data polygons of the id list for this part kind, then removes one further opaque triangle per listed
 * id and OMD (__SetPloyAlpha255To0 runs once per id; the markers it was meant for are already transparent). */
static void __SetDataPolyInfo(stMmdData *ioMmdp)
{
    PS2_FRAME(0x270);
    int list[0x81];   /* sp+0x30, -1 terminated */
    const int *src = 0;
    int n = 0;
    unsigned int k = (unsigned int)(ioMmdp->id / 1000);
    switch (k < 0x10 ? k : 14) {
    case 0: src = mmd_dp_k0; n = 2; break;
    case 1: src = mmd_dp_k1; n = 12; break;
    case 2: src = mmd_dp_k2; n = 21; break;
    case 3: src = mmd_dp_k3; n = 10; break;
    case 4: src = mmd_dp_k4; n = 2; break;
    case 8: case 9: case 10: case 11: case 12: src = mmd_dp_k8; n = 6; break;
    case 14: n = 0x80; for (int i = 0; i < 0x80; i++) list[i] = i; break;
    case 15: src = mmd_dp_k15; n = 13; break;
    default: break;   /* 5, 6, 7, 13: empty */
    }
    for (int i = 0; src && i < n; i++) list[i] = src[i];
    list[n] = -1;
    __SetDataPoly(&ioMmdp->OMD, list);
    for (int i = 0; i < (signed char)ioMmdp->OMD.num; i++) {
        struct_38528994 *h = (struct_38528994 *)DF_MDATA_GetData(ioMmdp->OMD.datap, i);
        for (int *p = list; *p != -1; p++) __SetPloyAlpha255To0(h);
    }
}

/* Loads package ioDatap into slot ioMmdp: one DF_MMM block sized exactly for header + OMD (+ data-poly buffer) +
 * TM2 packets + MI2 + OTH + the prm chunks. No null tests; kinds other than 1 / 2 keep id. */
void DF_MMD_SetData(stMmdData *ioMmdp, int inKind, int inID, void *ioDatap)
{
    PS2_FRAME(0xd0);
    struct_add4cfba *chunk[8];   /* sp+0xa4: chunks 1..8 */
    ioMmdp->kind = (unsigned char)inKind;
    stMmdHead *hdr = (stMmdHead *)DF_MDATA_GetData(ioDatap, 0);
    int total = (hdr->version < 0x110) ? 0x10 : hdr->hd_size;
    for (int n = 1; n < 9; n++) {
        struct_add4cfba *c = (struct_add4cfba *)DF_MDATA_GetData(ioDatap, n);
        chunk[n - 1] = c;
        if (n == 2) total += __CalcTM2BuffSize(c);
        else if (n == 1) total += (int)c->size + 0x280;
        else total += (int)c->size;
    }
    ioMmdp->datap = DF_MMM_Alloc(0, total);
    unsigned char *buf = (unsigned char *)ioMmdp->datap;
    int hd = (hdr->version < 0x110) ? 0x10 : hdr->hd_size;
    DF_MFUNC_CopyMemory(buf, hdr, hd >> 2);
    ioMmdp->mhp = (stMmdHead *)buf;
    stMmdOmdInfo *info = 0;
    if (!(hdr->version < 0x110))
        info = (stMmdOmdInfo *)((unsigned char *)ioMmdp->mhp + *(unsigned int *)((unsigned char *)ioMmdp->mhp + 0x20));
    buf += hd;
    buf = __RemapOMD(&ioMmdp->OMD, buf, chunk[0], chunk[4], info);
    buf = __RemapTM2(&ioMmdp->TM2, buf, chunk[1], chunk[5], ioMmdp->kind);
    buf = __RemapMI2(&ioMmdp->MI2, buf, chunk[2], chunk[6]);
    unsigned int sz = chunk[3]->size;
    DF_MFUNC_CopyMemory(buf, chunk[3], (int)sz >> 2);
    ioMmdp->OTH.datap = (struct_add4cfba *)buf;
    ioMmdp->OTH.num = (char)ioMmdp->OTH.datap->num;
    DF_MDATA_Remap(ioMmdp->OTH.datap);
    buf += sz;
    sz = chunk[7]->size;
    DF_MFUNC_CopyMemory(buf, chunk[7], (int)sz >> 2);
    ioMmdp->OTH.prmp = (struct_add4cfba *)buf;
    DF_MDATA_Remap(ioMmdp->OTH.prmp);
    switch (ioMmdp->kind) {
    case 2:
        ioMmdp->id = inID;
        break;
    case 1:
        ioMmdp->id = inID;
        __SetDataPolyInfo(ioMmdp);
        __MMD_OffsetTBP(ioMmdp, 0);
        break;
    }
    ioMmdp->f_set = 1;
    /* SyncDCache(ioMmdp->datap, (char *)ioMmdp->datap + total): no-op on the host */
}
