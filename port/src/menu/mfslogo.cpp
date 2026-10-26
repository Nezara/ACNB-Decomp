/* src/menu/mfslogo.cpp - the boot logo sequence and boot memory-card check of the menu overlay.
 * DF_MFSLOGO_NewSystem creates one parent-less stMobj (play_func only, dt = struct_ae80a014) and starts a chain of
 * play functions: __PlayFsLogoSystemMcCheck (card check: GetMcInfo / SetTitle(5) / TitleCheck with busy-waits on
 * Ac2_Mcf_Proc, then Mtitle_snum and the two title-check messages; an error dialog on a bad card) ->
 * __PlayPubLogoSystem (publisher logo) -> __PlayFsLogoSystem (FromSoftware logo; ends with stat = 5, play_func = 0).
 * Each logo has the same step machine: 0 load tex + sprites, 1 fade in 30 frames, 2 hold 90 frames (stat = 2),
 * 3 fade out 30 frames, -1 cleanup. The Open / Sync / CloseInstallerDialog statics are callbacks handed to
 * installer_SetDialogFunc.
 * Original quirks kept: the Pub logo writes step = -1 on entry (its logo never shows); the Fs logo is skipped when
 * Ac_GetDiscType() == 1 (US disc); the card busy-waits end on any non-zero Proc result, an error (-1) is not told
 * apart from success. */
#include <string.h>
#include "acnb/sdk.h"

typedef struct struct_ae80a014 FsLogoWork;   /* dt of the logo mobj */
#define FSL(o) ((FsLogoWork *)&(o)->dt)

/* OLMENU .data 0x1ede020 / 0x1ede030 (Pub) and 0x1eddfe0 / 0x1ede000 (Fs): {x, y, w, h} */
static const struct stMenuRect mfslogo_pub_rect = {0, 0, 0x280, 0x1c0};
static const int mfslogo_pub_uv[4] = {0, 0, 0x280, 0x1c0};
static const struct stMenuRect mfslogo_fs_rect[2] = {{0, 0, 0x200, 0x1c0}, {0x200, 0, 0x80, 0x1c0}};
static const int mfslogo_fs_uv[2][4] = {{0, 0, 0x200, 0x1c0}, {0, 0, 0x80, 0x1c0}};

/* "erase a mobj": two separate byte read-modify-writes, f_draw = 0 then f_erase = 1 */
static inline void mfslogo_erase(struct stMobj *p)
{
    p->f_draw = 0;
    p->f_erase = 1;
}

static void __PlayPubLogoSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    PS2_STACK(int[4], uv, 0x40);
    PS2_STACK(struct stMenuRect, rect, 0x50);
    FsLogoWork *d = FSL(ioObjp);
    int c, alpha;

    d->step = -1;   /* (sic) forced: the logo steps below never run */
    switch (d->step) {
    case -1:
        MenuTex_UnRegisterBndTexture(d->tm2p);   /* (sic) tm2p is NULL on the forced path */
        if (d->sprtp[0]) {
            mfslogo_erase(d->sprtp[0]);
            d->sprtp[0] = 0;
        }
        d->step = 0;
        ioObjp->play_func = (decltype(ioObjp->play_func))GFN(__PlayFsLogoSystem);
        break;
    case 3:
        c = d->count;
        d->count = c + 1;
        if (c < 30) {
            alpha = 0x80 - (d->count << 7) / 30;
        } else {
            d->count = 0;
            d->step = -1;
            alpha = 0;
        }
        d->sprtp[0]->dt.sprite.cur_col.a = alpha;
        break;
    case 2:
        c = d->count;
        d->count = c + 1;
        if (c >= 90) {
            ioObjp->stat = 2;
            d->count = 0;
            d->step++;
        }
        break;
    case 1:
        c = d->count;
        d->count = c + 1;
        if (c < 30) {
            alpha = (d->count << 7) / 30;
        } else {
            d->count = 0;
            d->step++;
            alpha = 0x80;
        }
        d->sprtp[0]->dt.sprite.cur_col.a = alpha;
        break;
    case 0:
        d->tm2p = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
        File_Read(0xbbb, -1, (char *)d->tm2p, 1);
        MenuTex_RegisterBndTexture(d->tm2p);
        memcpy(&rect, &mfslogo_pub_rect, sizeof(rect));
        memcpy(uv, mfslogo_pub_uv, sizeof(uv));
        d->sprtp[0] = DF_MOBJ_NXNewSprite(ioObjp, 0xdae, &rect, uv, 0, 1, 0, 0, 0);
        d->sprtp[0]->dt.sprite.cur_col.a = 0;
        d->count = 0;
        d->step++;
        break;
    }
}

static void __PlayFsLogoSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0xc0);
    PS2_STACK(int[2][4], uv, 0x80);
    PS2_STACK(struct stMenuRect[2], rect, 0xa0);
    FsLogoWork *d = FSL(ioObjp);
    int c, alpha;

    if (Ac_GetDiscType() == 1) d->step = -1;
    switch (d->step) {
    case -1:
        MenuTex_UnRegisterBndTexture(d->tm2p);
        for (int i = 0; i < 2; i++) {
            if (d->sprtp[i]) {
                mfslogo_erase(d->sprtp[i]);
                d->sprtp[i] = 0;
            }
        }
        ioObjp->stat = 5;
        ioObjp->play_func = 0;
        break;
    case 3:
        c = d->count;
        d->count = c + 1;
        if (c < 30) {
            alpha = 0x80 - (d->count << 7) / 30;
        } else {
            d->count = 0;
            d->step = -1;
            alpha = 0;
        }
        d->sprtp[0]->dt.sprite.cur_col.a = alpha;
        d->sprtp[1]->dt.sprite.cur_col.a = alpha;
        break;
    case 2:
        c = d->count;
        d->count = c + 1;
        if (c >= 90) {
            ioObjp->stat = 2;
            d->count = 0;
            d->step++;
        }
        break;
    case 1:
        c = d->count;
        d->count = c + 1;
        if (c < 30) {
            alpha = (d->count << 7) / 30;
        } else {
            d->count = 0;
            d->step++;
            alpha = 0x80;
        }
        d->sprtp[0]->dt.sprite.cur_col.a = alpha;
        d->sprtp[1]->dt.sprite.cur_col.a = alpha;
        break;
    case 0:
        d->tm2p = (unsigned char *)DF_MFUNC_Aligned(MenuWorkp, 0x40);
        File_Read(0xbbb, -1, (char *)d->tm2p, 1);
        MenuTex_RegisterBndTexture(d->tm2p);
        memcpy(rect, mfslogo_fs_rect, sizeof(rect));
        memcpy(uv, mfslogo_fs_uv, sizeof(uv));
        for (int i = 0; i < 2; i++) {
            d->sprtp[i] = DF_MOBJ_NXNewSprite(ioObjp, 0xdac + i, &rect[i], uv[i], 0, 1, 0, 0, 0);
            d->sprtp[i]->dt.sprite.cur_col.a = 0;
        }
        d->count = 0;
        d->step++;
        break;
    }
}

/* the card check's exit: stop the card library, free its 1 MB work buffer, go on to the Pub logo */
static inline void mfslogo_mc_finish(struct stMobj *ioObjp, FsLogoWork *d)
{
    d->step = 0;
    Ac2_Mcf_Stop();
    if (McfWorkBuffp) {
        DF_MMM_Free(0, McfWorkBuffp);
        McfWorkBuffp = 0;
    }
    ioObjp->play_func = (decltype(ioObjp->play_func))GFN(__PlayPubLogoSystem);
}

/* step 3's error dialog: string id from the title-check messages, yes/no dialog + beep */
static inline void mfslogo_mc_dialog(FsLogoWork *d, unsigned int strid)
{
    DF_MDIALOG_Open(2, DF_MSTR_GetInfo(MenuMng.language, 1, strid), 0, 0, 0, 0, 1, 0);
    Voice_Call(0x3f1, 0x7f);
    d->step++;
}

static void __PlayFsLogoSystemMcCheck(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    FsLogoWork *d = FSL(ioObjp);

    switch (d->step) {
    case 4: {
        int v = DF_MDIALOG_Sync();
        if (v == 0x14) d->step = 2;
        else if ((v & 4) == 4) mfslogo_mc_finish(ioObjp, d);
        break;
    }
    case 3: {
        int m1 = d->mcfMsg[0];
        if (m1 == 2 && d->mcfMsg[1] == 2) mfslogo_mc_dialog(d, 0x30e3f);
        else if (m1 == 8 && d->mcfMsg[1] == 8) mfslogo_mc_dialog(d, 0x30e3e);
        else if (m1 == 8 && d->mcfMsg[1] != 0) mfslogo_mc_dialog(d, 0x30e3c);
        else if (m1 != 0 && d->mcfMsg[1] == 8) mfslogo_mc_dialog(d, 0x30e3d);
        else mfslogo_mc_finish(ioObjp, d);
        break;
    }
    case 2:
        ResetMCardType(0);
        ResetMCardType(1);
        Ac2_Mcf_GetMcInfo();
        while (!Ac2_Mcf_Proc(1)) ;   /* (sic) busy-wait (beqz v0): an error (-1) ends it like success, unchecked */
        Ac2_Mcf_SetTitle(5);
        Ac2_Mcf_TitleCheck();
        while (!Ac2_Mcf_Proc(1)) ;
        Mtitle_snum = Ac2_Mcf_GetSnum(1);
        Mtitle_snum = Mtitle_snum + Ac2_Mcf_GetSnum(2);
        d->mcfMsg[0] = Ac2_Mcf_GetTitleCheckMsg(1);
        d->mcfMsg[1] = Ac2_Mcf_GetTitleCheckMsg(2);
        d->step++;
        break;
    case 1:
        d->step = 2;
        break;
    case 0:
        installer_SetDialogFunc((int (*)(void *, int))GFN(__OpenInstallerDialog),
                                (int (*)(void *, int))GFN(__SyncInstallerDialog),
                                (int (*)(void *, int))GFN(__CloseInstallerDialog), d);
        installer_Start();
        d->step++;
        break;
    }
}

static int __CloseInstallerDialog(void)
{
    PS2_FRAME(0x10);
    DF_MDIALOG_Close();
    return 1;
}

static int __SyncInstallerDialog(void)
{
    PS2_FRAME(0x10);
    int v = DF_MDIALOG_Sync();
    if (v == 0x14) return 1;
    if ((v & 4) == 4) return 2;
    return 0;
}

/* pArg (a0) is not used; param arrives in a1 (protofix) */
static int __OpenInstallerDialog(void *pArg, int param)
{
    PS2_FRAME(0x10);
    (void)pArg;
    switch ((unsigned int)param) {
    case 1:
        DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x31610), 0, 0, 0, 0, 0, 0);
        break;
    case 2:
        DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x31611), 0, 0, 0, 0, 0, 0);
        break;
    case 3:
        DF_MDIALOG_Open(2, DF_MSTR_GetInfo(MenuMng.language, 1, 0x3160f), 0, 0, 0, 0, 1, 0);
        Voice_Call(0x3f1, 0x7f);
        break;
    case 4:
        DF_MDIALOG_Open(2, DF_MSTR_GetInfo(MenuMng.language, 1, 0x31612), 0, 0, 0, 0, 1, 0);
        Voice_Call(0x3f1, 0x7f);
        break;
    case 5:
    case 6:
    case 7:
        DF_MDIALOG_Open(5, DF_MSTR_GetInfo(MenuMng.language, 1, 0x31510), 0, 0, 0, 0, 0, 0);
        break;
    default:
        break;
    }
    return 1;
}

struct stMobj *DF_MFSLOGO_NewSystem(void)
{
    PS2_FRAME(0x20);
    struct stMobj *p = DF_MOBJ_GetNewObject(0);
    if (!p) return 0;
    p->type = 0;
    p->stat = 0;   /* in SetDraw's delay slot */
    DF_MBACK_SetDraw(0);
    if (MenuMng.f_new_system == 0) {
        Ac2_Mcf_Init();
        McfWorkBuffp = DF_MMM_Alloc(0, 0x100000);
        /* inType 0 (asm `move a2, zero`); the result and a failed alloc are not checked */
        Ac2_Mcf_Start(&DF_MFSLOGO_NewSystem__ioCp1, &DF_MFSLOGO_NewSystem__ioCp2, 0, McfWorkBuffp);
        p->play_func = (decltype(p->play_func))GFN(__PlayFsLogoSystemMcCheck);
    } else {
        p->play_func = (decltype(p->play_func))GFN(__PlayPubLogoSystem);
    }
    return p;
}
