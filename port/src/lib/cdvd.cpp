/* lib/cdvd.cpp - the game's CD reader. Packed archives are addressed by file id: id < 100000 -> binder s_listf[0]
 * (AC.BIN), id >= 100000 -> s_listf[1] (DISC.BIN, index id - 100000). Cdvd_Read / ReadLsnBlind / ReadN store a request
 * in s_cdvd and either read it inside the call (block != 0: sceCdRead + sceCdSync(0)) or only arm the state machine
 * (step 1, isBusy 1) that the game polls through Cdvd_Exec (File_AsyncProc) and Cdvd_IsBusy. Cdvd_SetupFile reads the
 * two binder headers at boot, Cdvd_Init resets the state and libcdvd.
 * The exact field write order, the two-poll completion of an async read (1 -> 5 -> sceCdRead -> 6, next poll
 * sceCdSync(1) + sceCdGetError -> done) and the SDK call sequence are the load-completion timing of every replay:
 * keep them as in the original. Original oddities kept: s_cdvd.guard holds the request's `mode` argument (0 for every
 * caller, so the busy guard never engages); Cdvd_ReadLsnBlind returns 0 on success / 1 on a busy guard; Cdvd_ReadN
 * floors size >> 11 where Cdvd_Read rounds up; the aio completion stores the size in bytes as the sector count. */
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
struct StCdvdListFile s_listf[2];
struct StCdvd s_cdvd;
char cdvd_acbin_path[10] = "\\AC.BIN;1";
char cdvd_discbin_path[12] = "\\DISC.BIN;1";
#endif

#define CDVD_DISC_ID_BASE 100000

/* binder of a file id (index adjusted for DISC.BIN) */
static inline StBinderMng *cdvd_mng(unsigned int &id)
{
    if (id < CDVD_DISC_ID_BASE) return &s_listf[0].bindmng;
    id -= CDVD_DISC_ID_BASE;
    return &s_listf[1].bindmng;
}

/* (size + 0x7ff) / 2048 with C signed division (the asm's bgez / +0x7ff / sra fix-up) */
static inline int cdvd_sec_up(int size)
{
    return (size + 0x7ff) / 2048;
}

/* the request field writes shared by Cdvd_Read / ReadLsnBlind / ReadN (in the original's order) */
static void cdvd_arm(unsigned char mode, unsigned int lsn, int sec, char *pbuf)
{
    s_cdvd.guard = mode;
    s_cdvd.cdrmode.trycount = 0;
    s_cdvd.pcdlfile = &s_cdvd.cdlfile;
    s_cdvd.cdrmode.spindlctrl = 1;
    s_cdvd.cdrmode.datapattern = 0;
    s_cdvd.cdrmode.pad = 0;
    s_cdvd.step = 1;
    s_cdvd.isBusy = 1;
    s_cdvd.pcdlfile->lsn = lsn;
    s_cdvd.sec = sec;
    s_cdvd.bufp = pbuf;
    s_cdvd.result = 0;
    s_cdvd.exist = 0;
}

/* blocking read of the armed request: first sceCdRead, GetError only between failures; then sync + finish */
static void cdvd_read_block(char *pbuf)
{
    while (!sceCdRead(s_cdvd.pcdlfile->lsn, (unsigned int)s_cdvd.sec, pbuf, &s_cdvd.cdrmode)) sceCdGetError();
    sceCdSync(0);
    FlushCache(0);
    s_cdvd.guard = 0;
    s_cdvd.step = 0;
    s_cdvd.isBusy = 0;
    s_cdvd.result = s_cdvd.sec << 11;
}

/* result = 0, guard = step = isBusy = 0 (search / request end) */
static void cdvd_clear(void)
{
    s_cdvd.result = 0;
    s_cdvd.guard = 0;
    s_cdvd.step = 0;
    s_cdvd.isBusy = 0;
}

int Cdvd_IsBusy(void)
{
    return s_cdvd.isBusy != 0;
}

/* tail call in the original (no frame) */
int Cdvd_GetSizeByte(int id)
{
    unsigned int i = (unsigned int)id;
    StBinderMng *mng = cdvd_mng(i);
    return Binder_GetSize(mng, (int)i);
}

int Cdvd_GetSize2048(int id)
{
    PS2_FRAME(0x10);
    unsigned int i = (unsigned int)id;
    StBinderMng *mng = cdvd_mng(i);
    int s = Binder_GetSize(mng, (int)i);
    return cdvd_sec_up(s) << 11;
}

/* `j sceCdSync` with a0 = 1 (no frame) */
int Cdvd_CdSync(void)
{
    return sceCdSync(1);
}

/* searches ppath (the device prefix up to the first ':' stripped) into pcf (0 = s_cdvd.cdlfile).
 * block: retry while the drive reports an error (infinity == 0 accepts the error as success - original oddity);
 * block == 0: start an aio search (step 4 = search only; unreached). */
int Cdvd_SearchFile(struct struct_9b06e75c *pcf, char *ppath, int block, int infinity)
{
    PS2_FRAME(0x40);
    int start = 0;
    if (ppath[0] != 0) {
        for (;;) {
            if (ppath[start] == ':') {
                start++;
                break;
            }
            start++;
            if (ppath[start] == 0) break;
        }
    }
    if (ppath[start] == 0) start = 0;
    char *path = ppath + start;

    if (s_cdvd.guard != 0) return 0;
    s_cdvd.pcdlfile = pcf ? pcf : &s_cdvd.cdlfile;
    s_cdvd.guard = 0;
    s_cdvd.cdrmode.trycount = 0;
    s_cdvd.cdrmode.spindlctrl = 1;
    s_cdvd.cdrmode.datapattern = 0;
    s_cdvd.cdrmode.pad = 0;
    s_cdvd.step = 1;
    s_cdvd.isBusy = 1;
    s_cdvd.pcdlfile->lsn = 0;
    s_cdvd.sec = 0;
    s_cdvd.bufp = 0;
    s_cdvd.result = 0;
    s_cdvd.exist = 0;

    if (block != 0) {
        for (;;) {
            int r = sceCdSearchFile(s_cdvd.pcdlfile, path);
            if (r == 1) break;
            if (sceCdGetError() == 0) {
                cdvd_clear();
                return 0;
            }
            if (infinity == 0) break;
            if (r != 0) break;
        }
        cdvd_clear();
        return 1;
    }

    s_cdvd.step = 2;
    if (AioCdSearchFile(path, s_cdvd.pcdlfile)) {
        cdvd_clear();
        return 0;
    }
    s_cdvd.step = 4;
    return 1;
}

int Cdvd_IsFileExist(int id)
{
    PS2_FRAME(0x10);
    unsigned int i = (unsigned int)id;
    StBinderMng *mng = cdvd_mng(i);
    return Binder_IsExist(mng, (int)i, 0) >= 0;
}

int Cdvd_GetResult(void)
{
    return s_cdvd.result;
}

/* reads sec sectors (<= 0: the whole file, rounded up) of file id from sector ofssec */
int Cdvd_Read(int id, unsigned int ofssec, int sec, char *pbuf, unsigned char mode, int block)
{
    PS2_FRAME(0x80);
    unsigned int i = (unsigned int)id;
    StBinderMng *mng = cdvd_mng(i);
    if (Binder_IsExist(mng, (int)i, 0) < 0) return 0;

    i = (unsigned int)id;
    mng = cdvd_mng(i);
    int off = Binder_GetOffset(mng, (int)i);
    unsigned int base = off < 0 ? 0 : ((unsigned int)off >> 11) + mng->lsn;
    unsigned int lsn = ofssec + base;
    if (sec <= 0) {
        i = (unsigned int)id;
        StBinderMng *m2 = cdvd_mng(i);
        sec = cdvd_sec_up(Binder_GetSize(m2, (int)i));
    }
    sec -= (int)ofssec;   /* always (delay slot of the guard test) */

    if (s_cdvd.guard != 0) return 0;
    cdvd_arm(mode, lsn, sec, pbuf);
    if (block != 0) cdvd_read_block(pbuf);
    return 1;
}

/* reads a plain disc file by name (sec <= 0: size >> 11, truncated) */
int Cdvd_ReadN(char *pfname, int sec, char *pbuf, unsigned char mode, int block)
{
    PS2_FRAME(0x40);
    if (s_cdvd.guard != 0) return 0;
    cdvd_arm(mode, 0, sec, pbuf);

    if (block != 0) {
        if (!Cdvd_SearchFile(s_cdvd.pcdlfile, pfname, 1, 1)) {
            cdvd_clear();
            return 0;
        }
        /* the search cleared the request: write it again (lsn = what the search found, re-read first) */
        struct struct_9b06e75c *pf = s_cdvd.pcdlfile;
        unsigned int lsn = pf->lsn;
        s_cdvd.pcdlfile = pf;
        s_cdvd.guard = mode;
        s_cdvd.cdrmode.trycount = 0;
        s_cdvd.cdrmode.spindlctrl = 1;
        s_cdvd.cdrmode.datapattern = 0;
        s_cdvd.cdrmode.pad = 0;
        s_cdvd.step = 1;
        s_cdvd.isBusy = 1;
        s_cdvd.pcdlfile->lsn = lsn;
        s_cdvd.sec = sec;
        s_cdvd.bufp = pbuf;
        s_cdvd.result = 0;
        s_cdvd.exist = 0;
        if (s_cdvd.sec <= 0) s_cdvd.sec = (int)s_cdvd.pcdlfile->size / 2048;
        cdvd_read_block(s_cdvd.bufp);
        return 1;
    }

    s_cdvd.step = 2;
    if (AioCdSearchFile(pfname, s_cdvd.pcdlfile)) {   /* the unstripped name (Cdvd_SearchFile passes the stripped one) */
        cdvd_clear();
        return 0;
    }
    s_cdvd.step = 3;
    return 1;
}

/* reads the whole file id (no existence check); returns 0 when armed / read, 1 when the guard byte was set */
int Cdvd_ReadLsnBlind(unsigned int id, char *pbuf, unsigned char mode, int block)
{
    PS2_FRAME(0x70);
    unsigned int i = id;
    StBinderMng *mng = cdvd_mng(i);
    int off = Binder_GetOffset(mng, (int)i);
    unsigned int lsn = off < 0 ? 0 : ((unsigned int)off >> 11) + mng->lsn;
    i = id;
    StBinderMng *m2 = cdvd_mng(i);
    int sec = cdvd_sec_up(Binder_GetSize(m2, (int)i));

    if (s_cdvd.guard != 0) return 1;
    cdvd_arm(mode, lsn, sec, pbuf);
    if (block != 0) cdvd_read_block(pbuf);
    return 0;
}

/* one poll step of an async request; non-zero = run again in the same Cdvd_Exec */
static int __CdvdAsyncProc(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(int, cmd, 0x1c);
    PS2_STACK(int, result, 0x18);
    cmd = 0;
    result = 0;
    switch (s_cdvd.step) {
    case 6:   /* read issued */
        if (sceCdSync(1)) return 0;
        if (sceCdGetError()) {
            s_cdvd.step = 5;
            return 1;
        }
        s_cdvd.guard = 0;
        s_cdvd.step = 0;
        s_cdvd.isBusy = 0;
        s_cdvd.result = s_cdvd.sec << 11;
        FlushCache(0);
        return 0;
    case 5:   /* issue the read */
        while (!sceCdRead(s_cdvd.pcdlfile->lsn, (unsigned int)s_cdvd.sec, s_cdvd.bufp, &s_cdvd.cdrmode)) sceCdGetError();
        s_cdvd.step = 6;
        return 0;
    case 4:   /* aio search running (unreached) */
    case 3:
        if (!AioCmdSync(1, &cmd, &result)) return 1;
        if (result < 0) {
            s_cdvd.step = 0;   /* isBusy stays 1 */
            return 0;
        }
        s_cdvd.exist = (s_cdvd.pcdlfile->size != 0 && s_cdvd.pcdlfile->lsn != 0) ? 1 : 0;
        if (s_cdvd.sec <= 0) s_cdvd.sec = (int)s_cdvd.pcdlfile->size;   /* bytes, not sectors (original) */
        if (s_cdvd.step == 4) cdvd_clear();
        else s_cdvd.step = 5;
        return 1;
    case 1:
        s_cdvd.step = 5;
        return 1;
    default:
        return 0;
    }
}

void Cdvd_Exec(void)
{
    PS2_FRAME(0x10);
    while (__CdvdAsyncProc()) {
    }
}

int Cdvd_Init(void)
{
    PS2_FRAME(0x10);
    ZeroMemory(&s_cdvd, 0x44);
    s_cdvd.pcdlfile = &s_cdvd.cdlfile;
    while (!sceCdInit(0)) {
    }
    sceCdMmode(2);
    return 1;
}

/* finds AC.BIN / DISC.BIN, reads their binder headers (0x44 / 1 sectors) and initialises the two binders; the search
 * and read retry forever without GetError */
static void cdvd_setup_one(int n, char *path, char *hdr, unsigned int nsec)
{
    s_listf[n].pbufHeader = hdr;
    s_cdvd.pcdlfile = &s_cdvd.cdlfile;
    while (!sceCdSearchFile(s_cdvd.pcdlfile, path)) {
    }
    FlushCache(0);
    char *buf = s_listf[n].pbufHeader;
    unsigned int lsn = s_cdvd.pcdlfile->lsn;
    s_cdvd.cdrmode.spindlctrl = 1;
    s_cdvd.cdrmode.trycount = 0;
    s_cdvd.cdrmode.datapattern = 0;
    s_cdvd.cdrmode.pad = 0;
    FlushCache(0);
    while (!sceCdRead(lsn, nsec, buf, &s_cdvd.cdrmode)) {
    }
    sceCdSync(0);
    FlushCache(0);
    Binder_Init(&s_listf[n].bindmng, s_listf[n].pbufHeader, s_cdvd.pcdlfile->lsn);
}

int Cdvd_SetupFile(void)
{
    PS2_FRAME(0x30);
    cdvd_setup_one(0, cdvd_acbin_path, s_bufGeneralHeader, 0x44);
    cdvd_setup_one(1, cdvd_discbin_path, s_bufPeculiarHeader, 1);
    return 1;
}
