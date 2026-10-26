/* src/menu/msys.cpp — Memory Card / Save-Load / System sub-screens of the OLMENU overlay. Every screen is an stMobj whose
 * dt union holds a private struct and whose play_func walks a small state machine on a `step` byte (fade-in -> idle ->
 * dialog / save / load steps -> fade-out -> next screen). Families: the Emblem file list (EMBDATA_* entries,
 * __*McEmbSystem*), the Game data file list (GAMEDATA_*, __*McGameSystem*), the MC shell that drives the mcf.c
 * memory-card engine (Ac2_Mcf_*, interpreted) and the System top menu / title emblem-load selector.
 * Dialogs: DF_MSTR_GetInfo(MenuMng.language, 1, id) + DF_MDIALOG_Open, polled through __SyncDialog.
 * Native so far: groups 1-2 (the whole Emblem list: entries, Load / Save, card-pulled dialog, list build, idle cursor,
 * fade in / out, constructor; __SyncDialog), group 3 (Game data list entries GAMEDATA_*, card-pulled dialog, cursor
 * select, fade-out start), group 4 (Game data Load / Save, list build), group 5 (Game data list idle / fade in / out /
 * constructor, the MC_MODEL card models, memory-card engine start / stop / poll), group 6 (the MC shell: card selection,
 * format / alert dialogs, dispatch, fade in / out, constructors, DF_MSYS_NewMcSystem*), group 7 (the title emblem-load
 * selector TITLESELECT_* / __PlayEmblemLoadSystem*, __DlgSystemDisableLoad, msys_FileReadSync, DF_InitMemCard with its
 * port hook); the rest is still interpreted. */
#include <string.h>
#include "acnb/ps2fp.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/hooks.h"

#ifdef ACNB_SOURCEPORT
char msys_fmt_ita[8] = "ita_%d";
char msys_fmt_ld[4] = "%ld";
char msys_corrupt[16] = "Corrupt File";
char msys_up_0[8] = "up_0";
char msys_down_0[8] = "down_0";
#endif

typedef struct struct_46d019a2 McEmbSys;   /* Emblem list screen (dt) */
typedef struct struct_e8636e83 EmbEntry;   /* one Emblem file entry (dt) */

static inline McEmbSys *msys_emb_sys(struct stMobj *o) { return (McEmbSys *)&o->dt; }
static inline EmbEntry *msys_emb_ent(struct stMobj *o) { return (EmbEntry *)&o->dt; }

static inline void msys_colour(struct struct_76b810ec *c, unsigned int w)
{
    c->r = (unsigned char)w;
    c->g = (unsigned char)(w >> 8);
    c->b = (unsigned char)(w >> 16);
    c->a = (unsigned char)(w >> 24);
}

static inline void msys_erase(struct stMobj *p)
{
    p->f_draw = 0;
    p->f_erase = 1;
}

/* dialog with a message string: DF_MDIALOG_Open(type, msg, 0, 0, 0, 0, idx, fbtnoff) */
static inline void msys_dlg(int type, unsigned int id, int idx, int fbtnoff)
{
    DF_MDIALOG_Open(type, DF_MSTR_GetInfo(MenuMng.language, 1, id), 0, 0, 0, 0, idx, fbtnoff);
}

/* inlined "fade out one emblem entry" (pSelect is not NULL-tested once e is set) */
static void msys_emb_fadeout(struct stMobj *e)
{
    if (!e) return;
    EmbEntry *d = msys_emb_ent(e);
    if (d->pWnd) WindowEx_SetFadeOut(d->pWnd);
    DF_MOBJ_SetDrawEnable(d->pSelect, 0);
    DF_MOBJ_SetFadeOut(d->pPad);
    DF_MOBJ_SetFadeOut(d->pEmblem);
    DF_MOBJ_SetFadeOut(d->pNew);
    DF_MOBJ_SetFadeOut(d->pData);
    DF_MOBJ_SetFadeOut(d->pBroken);
}

/* inlined "fade in one emblem entry" (caller tests e; no pSelect) */
static void msys_emb_fadein(struct stMobj *e)
{
    EmbEntry *d = msys_emb_ent(e);
    if (d->pWnd) WindowEx_SetFadeIn(d->pWnd);
    DF_MOBJ_SetFadeIn(d->pPad);
    DF_MOBJ_SetFadeIn(d->pEmblem);
    DF_MOBJ_SetFadeIn(d->pNew);
    DF_MOBJ_SetFadeIn(d->pData);
    DF_MOBJ_SetFadeIn(d->pBroken);
}

/* inlined exit of the Emblem list (Load / Save / DlgPull / __PlayMcEmbSystem): fade everything out */
static void msys_emb_exit(struct stMobj *ioObjp)
{
    McEmbSys *dt = msys_emb_sys(ioObjp);
    DF_MOBJ_SetFadeOut(dt->pText);
    msys_emb_fadeout(dt->pEmbPC);
    for (int i = 0; i < 8; i++) msys_emb_fadeout(dt->pEmbMC[i]);
    DF_M1HELP_FadeOut();
    Voice_Call(0x3ed, 0x7f);
    dt->step = 0;
    ioObjp->count = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = (void (*)(void))GFN(__PlayMcEmbSystemFadeOut);
}

/* pad decide / cancel on the result dialog: voices, close, next step (Emb and Game Load / Save, steps 8 / 0x5c) */
static inline void msys_pad_close(char *step)
{
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    unsigned char dw = pad->Rdw;
    if (dw != 1 && pad->Rrt != 1) return;
    if (dw == 1) Voice_Call(0x3ea, 0x7f);
    if (MPad[MPadUser].Rrt == 1) Voice_Call(0x3eb, 0x7f);
    DF_MDIALOG_Close();
    (*step)++;
}

static inline void msys_emb_pad_close(McEmbSys *dt) { msys_pad_close(&dt->step); }

static void __EMBDATA_SetEmb(struct stMobj *ioObjp, int No)
{
    PS2_FRAME(0xe0);
    PS2_STACK(struct stMenuRect, rec, 0xb0);   /* stale when pWnd is NULL */
    if (!ioObjp) return;
    EmbEntry *dt = msys_emb_ent(ioObjp);
    dt->bNew = 0;
    dt->bBroken = 0;
    /* uv must sit at its 16-aligned original slot: NXNewSprite copies it with lq (address aligned down) */
    PS2_STACK(int[4], uv, 0xc0);
    uv[0] = 0;
    uv[1] = 0;
    uv[2] = 0x80;
    uv[3] = 0x80;
    PS2_STACK(struct struct_76b810ec, col, 0xdc);
    msys_colour(&col, 0x80808080u);
    /* texInfo (GS TEX0 / TEX1 bitfields, final values of MWCC's read-modify-write sequence) */
    unsigned long long *ti = (unsigned long long *)((char *)ioObjp + 0x40);
    ti[0] = 0x4005e405dd30aee0ULL;   /* tbp0 0x2ee0 tbw 2 psm 0x13 tw 7 th 7 tcc 1 tfx 0 cbp 0x2f20 cpsm 0 csm 0 csa 0 cld 2 */
    ti[1] = (ti[1] & ~0x00000fff001803fdULL) | 0x60ULL;   /* lcm 0 mxl 0 mmag 1 mmin 1 mtba 0 l 0 k 0 */
    if (dt->pWnd) WindowEx_GetRect(dt->pWnd, &rec);
    rec.x += 6;
    rec.y += 6;
    rec.w -= 12;
    rec.h -= 12;
    char *tex = No < 100 ? (char *)Mcf_Start_Info + No * 0x4420 : (char *)EmblemTM2 + (No - 100) * 0x4420;
    if (dt->pEmblem) {
        dt->pEmblem->f_draw = 0;
        dt->pEmblem->f_erase = 1;
        dt->pEmblem = 0;
    }
    if (dt->texId != -1) MenuTex_UnRegisterSingleTex(dt->texId);
    dt->texId = MenuTex_RegisterSingleTex((struct struct_c53f69de *)tex);
    dt->pEmblem = DF_MOBJ_NXNewSprite(ioObjp, dt->texId, &rec, uv, &col, 1, 0, 1, 0);
    DF_MOBJ_SetDrawEnable(dt->pNew, 0);
    DF_MOBJ_SetDrawEnable(dt->pData, 0);
    DF_MOBJ_SetDrawEnable(dt->pBroken, 0);
}

static void __EMBDATA_Delete(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    if (!ioObjp) return;
    EmbEntry *dt = msys_emb_ent(ioObjp);
    if (dt->texId != -1) MenuTex_UnRegisterSingleTex(dt->texId);
    dt->texId = -1;
    if (dt->pWnd) WindowEx_Delete(dt->pWnd);
    dt->pWnd = 0;
    UnitSprite_Delete(dt->pSelect);   /* unchecked; pSelect left dangling */
    if (dt->pPad) { msys_erase(dt->pPad); dt->pPad = 0; }
    if (dt->pEmblem) { msys_erase(dt->pEmblem); dt->pEmblem = 0; }
    if (dt->pNew) { msys_erase(dt->pNew); dt->pNew = 0; }
    if (dt->pData) { msys_erase(dt->pData); dt->pData = 0; }
    if (dt->pBroken) { msys_erase(dt->pBroken); dt->pBroken = 0; }
    msys_erase(ioObjp);
}

static struct stMobj *__EMBDATA_New(struct stMobj *inObjp, int No, int Pos)
{
    static const int s_base[3][2] = {{0xb5, 0xa0}, {0xae, 0xac}, {0xa5, 0x96}};   /* OLMENU 0x1ee0270 */
    static const int s_id[3] = {0x192e1, 0x192e2, 0x1895c};                       /* OLMENU 0x1ee0288 */
    PS2_FRAME(0x100);
    PS2_STACK(char[32], buff, 0x90);             /* sprintf destination / partsunitname */
    PS2_STACK(int[4], uv, 0xb0);                 /* never written: stale words */
    PS2_STACK(struct stMenuRect, rec, 0xc0);     /* stale when pWnd is NULL */
    struct stMobj *o = DF_MOBJ_GetNewObject(inObjp);
    if (!o) return 0;
    o->type = 0;
    o->stat = 0;
    EmbEntry *dt = msys_emb_ent(o);
    dt->No = No;
    dt->Pos = Pos;
    dt->bNew = 1;
    dt->bBroken = 0;
    dt->texId = -1;
    struct struct_76b810ec back;
    msys_colour(&back, 0x40000000u);
    ps2_sprintf_d(buff, msys_fmt_ita, Pos < 0 ? 0 : Pos + 1);
    dt->pWnd = WindowEx_NewFromLayout(o, (_WINDOWEX_TYPE)0, (char *)LAYOUT_GetLayoutData(0x425), buff);
    dt->pSelect = UnitSpriteBlink_New(o, (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x425), (char *)"select_0");
    dt->pSelect->draw_prio = 3;   /* unchecked */
    int x = (Pos % 4) * 120;
    int y = (Pos / 4) * 120;
    struct struct_214acd28 v;
    v.x = x;
    v.y = y;
    UnitSprite_SetPosition(dt->pSelect, &v);
    DF_MOBJ_SetDrawEnable(dt->pSelect, 0);
    if (dt->pWnd) WindowEx_GetRect(dt->pWnd, &rec);
    rec.x += 6;
    rec.y += 6;
    rec.w -= 12;
    rec.h -= 12;
    dt->pPad = DF_MOBJ_NXNewSprite(o, -1, &rec, uv, &back, 0, 0, 0, 0);
    if (Pos < 0) return o;
    struct struct_76b810ec col_str, col_bad;
    msys_colour(&col_str, 0x808c826eu);
    msys_colour(&col_bad, 0x80000080u);
    dt->pNew = DF_MOBJ_NewString(o, 0, s_id[0], 0, 1, s_base[0][0] + x, s_base[0][1] + y, &col_str, 0, 0);
    dt->pData = DF_MOBJ_NewString(o, 0, s_id[1], 0, 1, s_base[1][0] + x, s_base[1][1] + y, &col_str, 0, 0);
    dt->pBroken = DF_MOBJ_NewString(o, 0, s_id[2], 0, 3, s_base[2][0] + x, s_base[2][1] + y, &col_bad, 0, 0);
    DF_MOBJ_SetDrawEnable(dt->pBroken, 0);
    return o;
}

static void __LoadMcEmbSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    PS2_STACK(int, result, 0x6c);
    McEmbSys *dt = msys_emb_sys(ioObjp);
    int mcIdx = dt->mcIdx;
    if (GetPortStatus(mcIdx) == 0 || Mcf_Proc_Ret == -1) {
        /* card pulled / engine error */
        if (__SyncDialog(0) == 2) DF_MDIALOG_Close();
        if (__SyncDialog(0) < 4) return;
        if (dt->stat >= 2) {
            msys_emb_exit(ioObjp);
            return;
        }
        dt->dlgMsgId = dt->stat == 1 ? 0x30e18 : 0x30e0b;
        dt->step = 0;
        ioObjp->play_func = (void (*)(void))GFN(__DlgMcEmbSystemPull);
        Ac2_Mcf_GetMcInfo();
        return;
    }
    struct stMobj *slot;
    switch (dt->step) {
    case 0:
        dt->stat = 0;
        slot = dt->pEmbMC[dt->idx];
        if (slot && msys_emb_ent(slot)->bBroken) {
            msys_dlg(5, 0x30e41, 0, 0);
            dt->step++;
        } else {
            dt->step += 2;
        }
        break;
    case 1:
        if (__SyncDialog(0) >= 4) {
            dt->step = 0;
            ioObjp->play_func = (void (*)(void))GFN(__PlayMcEmbSystem);
            ioObjp->count = 0;
        }
        break;
    case 2:
        msys_dlg(2, 0x30e16, 1, 0);
        dt->step++;
        break;
    case 3:
        if (__SyncDialog(&result) >= 4) {
            if (result == 1) {
                dt->step++;
            } else {
                dt->step = 0;
                ioObjp->play_func = (void (*)(void))GFN(__PlayMcEmbSystem);
                ioObjp->count = 0;
            }
        }
        break;
    case 4:
        dt->stat = 1;
        if (!dt->pSave) dt->pSave = DF_MMM_Alloc(0, 0x4420);
        ZeroMemory32b(dt->pSave, 0x1108);
        dt->mcInfop->datap = dt->pSave;
        slot = dt->pEmbMC[dt->idx];
        result = Ac2_Mcf_Load(mcIdx + 1, slot ? msys_emb_ent(slot)->No : -1);
        if (result == -1) {
            dt->step = 0x5a;
        } else {
            msys_dlg(5, 0x30e17, 0, 1);
            ioObjp->count = 0;
            dt->step++;
        }
        break;
    case 5:
        ioObjp->count++;
        if (Mcf_Proc_Ret != 0) {
            if (Mcf_Proc_Ret == -1) dt->step = 0x5a;
            else dt->step++;
        }
        break;
    case 6:
        ioObjp->count++;
        if (ioObjp->count < 15) break;
        if (__SyncDialog(0) == 2) DF_MDIALOG_Close();
        if (__SyncDialog(0) >= 4) {
            msys_dlg(4, 0x30e19, 0, 1);
            memcpy((char *)EmblemTM2 + dt->player * 0x4420, dt->pSave, 0x4420);
            dt->stat = 2;
            dt->step++;
        }
        break;
    case 7:
        ioObjp->count++;
        if (ioObjp->count >= 15 && __SyncDialog(0) == 2) dt->step++;
        break;
    case 8:
        msys_emb_pad_close(dt);
        break;
    case 9:
        if (__SyncDialog(0) >= 4) dt->step = 0x64;
        break;
    case 0x5a:
        if (__SyncDialog(0) == 2) DF_MDIALOG_Close();
        if (__SyncDialog(0) >= 4) {
            msys_dlg(5, 0x30e18, 0, 1);
            ioObjp->count = 0;
            dt->step++;
        }
        break;
    case 0x5b:
        ioObjp->count++;
        if (ioObjp->count >= 15 && __SyncDialog(0) == 2) dt->step++;
        break;
    case 0x5c:
        msys_emb_pad_close(dt);
        break;
    case 0x5d:
        if (__SyncDialog(0) >= 4) {
            Ac2_Mcf_GetMcInfo();
            Ac2_Mcf_Proc(0);
            dt->step = 0x64;
        }
        break;
    default:   /* 0x64 and unknown steps */
        msys_emb_exit(ioObjp);
        break;
    }
}

static void __DlgMcEmbSystemPull(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    McEmbSys *dt = msys_emb_sys(ioObjp);
    if (dt->step == 1) {
        if (__SyncDialog(0) >= 4) {
            SetForceMCardStatus(dt->mcIdx, 0);
            msys_emb_exit(ioObjp);
        }
    } else if (dt->step == 0) {
        msys_dlg(5, dt->dlgMsgId, 0, 0);
        dt->step++;
    }
}

static void __MakeMcEmbSystemList(struct stMobj *ioObjp)
{
    PS2_FRAME(0xc0);
    McEmbSys *dt = msys_emb_sys(ioObjp);
    int nlist[8] = {0, 1, 2, 3, 4, 5, 6, 7};   /* OLMENU 0x1ee0250 */
    int fnum = 0;
    int *ret = mc_ret[(int)dt->mcIdx];
    for (int i = 0; i < 8; i++) {
        if (ret[i] == -1) continue;
        if (GetGameFileBroken(i) || GetIconFileBroken(i)) {
            struct stMobj *slot = dt->pEmbMC[fnum] = __EMBDATA_New(ioObjp, i, fnum);
            if (slot) {
                EmbEntry *e = msys_emb_ent(slot);
                e->bNew = 0;
                e->bBroken = 1;
                DF_MOBJ_SetDrawEnable(e->pNew, 0);
                DF_MOBJ_SetDrawEnable(e->pData, 0);
                DF_MOBJ_SetDrawEnable(e->pBroken, 1);
            }
        } else {
            dt->pEmbMC[fnum] = __EMBDATA_New(ioObjp, i, fnum);
            __EMBDATA_SetEmb(dt->pEmbMC[fnum], i);
        }
        if (dt->pEmbMC[fnum]) msys_emb_fadein(dt->pEmbMC[fnum]);
        nlist[i] = -1;
        fnum++;
    }
    if (dt->bSave && fnum < 8) {
        int j = 0;
        while (j < 8 && nlist[j] == -1) j++;
        if (j < 8) {
            dt->pEmbMC[fnum] = __EMBDATA_New(ioObjp, nlist[j], fnum);
            if (dt->pEmbMC[fnum]) msys_emb_fadein(dt->pEmbMC[fnum]);
            fnum++;
        }
    }
    if (dt->pEmbMC[0]) DF_MOBJ_SetDrawEnable(msys_emb_ent(dt->pEmbMC[0])->pSelect, 1);
    dt->dataNum = fnum;
    dt->pEmbPC = __EMBDATA_New(ioObjp, -1, -1);
    __EMBDATA_SetEmb(dt->pEmbPC, dt->player + 100);
    if (dt->pEmbPC) msys_emb_fadein(dt->pEmbPC);
    Voice_Call(0x3ec, 0x7f);
}

static int __SyncDialog(int *pResult)
{
    PS2_FRAME(0x20);
    int s = DF_MDIALOG_Sync();
    int n = s & 0xf;
    if (n == 4) {
        if (pResult) *pResult = (s & 0x10) ? 1 : (s & 0x20) ? -1 : 0;
        return 4;
    }
    if (n >= 1 && n <= 3) return n;
    return 0;
}

/* ---- group 2: Save, idle cursor, fade in / out, constructor ---- */

static void __SaveMcEmbSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    PS2_STACK(int, result, 0x6c);
    McEmbSys *dt = msys_emb_sys(ioObjp);
    int mcIdx = dt->mcIdx;
    if (GetPortStatus(mcIdx) == 0 || Mcf_Proc_Ret == -1) {
        /* card pulled / engine error */
        if (__SyncDialog(0) == 2) DF_MDIALOG_Close();
        if (__SyncDialog(0) < 4) return;
        if (dt->stat >= 2) {
            msys_emb_exit(ioObjp);
            return;
        }
        dt->dlgMsgId = dt->stat == 1 ? 0x30e1b : 0x30e0b;
        dt->step = 0;
        ioObjp->play_func = (void (*)(void))GFN(__DlgMcEmbSystemPull);
        Ac2_Mcf_GetMcInfo();
        return;
    }
    struct stMobj *slot;
    switch (dt->step) {
    case 0: {
        dt->stat = 0;
        slot = dt->pEmbMC[dt->idx];
        int isnew = slot ? msys_emb_ent(slot)->bNew : 0;
        if (isnew && dt->mcInfop->free_size < 0x3c) {   /* not enough space for a new file */
            msys_dlg(5, 0x30e1c, 0, 0);
            dt->step++;
        } else {
            dt->step += 2;
        }
        break;
    }
    case 1:
        if (__SyncDialog(0) >= 4) {
            dt->step = 0;
            ioObjp->play_func = (void (*)(void))GFN(__PlayMcEmbSystem);
            ioObjp->count = 0;
        }
        break;
    case 2:
        slot = dt->pEmbMC[dt->idx];
        if (slot ? msys_emb_ent(slot)->bNew : 0) msys_dlg(2, 0x30e10, 1, 0);
        else msys_dlg(2, 0x30e11, 1, 0);
        dt->step++;
        break;
    case 3:
        if (__SyncDialog(&result) >= 4) {
            if (result == 1) {
                dt->step++;
            } else {
                dt->step = 0;
                ioObjp->play_func = (void (*)(void))GFN(__PlayMcEmbSystem);
                ioObjp->count = 0;
            }
        }
        break;
    case 4: {
        dt->stat = 1;
        dt->mcInfop->datap = EmblemTM2[(int)dt->player];
        slot = dt->pEmbMC[dt->idx];
        int no;
        if (slot ? msys_emb_ent(slot)->bNew : 0) no = -1;
        else if (!slot) no = -1;
        else no = msys_emb_ent(slot)->No;
        result = Ac2_Mcf_Save(mcIdx + 1, no);
        if (result == -1) {
            dt->step = 0x5a;
        } else {
            msys_dlg(5, 0x30e12, 0, 1);
            ioObjp->count = 0;
            dt->step++;
        }
        break;
    }
    case 5:
        ioObjp->count++;
        if (Mcf_Proc_Ret != 0) {
            if (Mcf_Proc_Ret == -1) dt->step = 0x5a;
            else dt->step++;
        }
        break;
    case 6:
        ioObjp->count++;
        if (ioObjp->count >= 15) {
            if (__SyncDialog(0) == 2) DF_MDIALOG_Close();
            if (__SyncDialog(0) >= 4) {
                msys_dlg(4, 0x30e14, 0, 1);
                __EMBDATA_SetEmb(dt->pEmbMC[dt->idx], dt->player + 100);   /* the player's own emblem in the slot */
                dt->stat = 2;
                dt->step++;
            }
        }
        break;
    case 7:
        ioObjp->count++;
        if (ioObjp->count >= 15 && __SyncDialog(0) == 2) dt->step++;
        break;
    case 8:
        msys_emb_pad_close(dt);
        break;
    case 9:
        if (__SyncDialog(0) >= 4) dt->step = 0x64;
        break;
    case 0x5a:
        if (__SyncDialog(0) == 2) DF_MDIALOG_Close();
        if (__SyncDialog(0) >= 4) {
            msys_dlg(5, 0x30e1b, 0, 1);
            ioObjp->count = 0;
            dt->step++;
        }
        break;
    case 0x5b:
        ioObjp->count++;
        if (ioObjp->count >= 15 && __SyncDialog(0) == 2) dt->step++;
        break;
    case 0x5c:
        msys_emb_pad_close(dt);
        break;
    case 0x5d:
        if (__SyncDialog(0) >= 4) {
            Ac2_Mcf_GetMcInfo();
            Ac2_Mcf_Proc(0);
            dt->step = 0x64;
        }
        break;
    default:   /* 0x64 and unknown steps */
        msys_emb_exit(ioObjp);
        break;
    }
}

static void __PlayMcEmbSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    McEmbSys *dt = msys_emb_sys(ioObjp);
    if (dt->step == 1) {
        ioObjp->stat = 5;
        ioObjp->play_func = 0;
    } else if (dt->step == 0) {
        ioObjp->count++;
        if (ioObjp->count < 5) return;
        if (dt->pSave) DF_MMM_Free(0, dt->pSave);
        dt->pSave = 0;
        if (dt->pText) {
            msys_erase(dt->pText);
            dt->pText = 0;
        }
        __EMBDATA_Delete(dt->pEmbPC);
        dt->pEmbPC = 0;
        for (int i = 0; i < 8; i++) {
            __EMBDATA_Delete(dt->pEmbMC[i]);
            dt->pEmbMC[i] = 0;
        }
        dt->step++;
    }
}

/* idle state of the list: key guide, card check, cancel / decide, cursor on a 4 x 2 grid that wraps inside the
 * row / column that actually has entries */
static void __PlayMcEmbSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x80);
    McEmbSys *dt = msys_emb_sys(ioObjp);
    if (MenuKeyGuide_IsShown()) return;
    if (MPad[MPadUser].start == 1) {
        MenuKeyGuide_Show(dt->bSave ? 0x34 : 0x37);
        return;
    }
    if (GetPortStatus(dt->mcIdx) == 0 || Mcf_Proc_Ret == -1) {
        dt->dlgMsgId = 0x30e0b;
        dt->step = 0;
        ioObjp->play_func = (void (*)(void))GFN(__DlgMcEmbSystemPull);
        Ac2_Mcf_GetMcInfo();
        return;
    }
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    if (pad->Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        msys_emb_exit(ioObjp);
        return;
    }
    if (dt->dataNum <= 0) return;
    if (pad->Rdw == 1) {
        Voice_Call(0x3ea, 0x7f);
        dt->stat = 0;
        dt->step = 0;
        ioObjp->count = 0;
        if (dt->bSave) ioObjp->play_func = (void (*)(void))GFN(__SaveMcEmbSystem);
        else ioObjp->play_func = (void (*)(void))GFN(__LoadMcEmbSystem);
        return;
    }
    if (dt->dataNum < 2) return;
    int old = dt->idx;
    int rowcnt[2] = {0, 0};          /* entries per row (sp+0x78) */
    int colcnt[4] = {0, 0, 0, 0};    /* entries per column (sp+0x60) */
    int y = dt->idx / 4;
    int x = dt->idx % 4;
    for (int i = 0; i < dt->dataNum; i++) {
        rowcnt[i / 4]++;
        colcnt[i % 4]++;
    }
    struct struct_81b11733 *rep = &MPad[MPadUser].repeat;
    if (rep->lt == 1) {
        if (--x < 0) x = rowcnt[y] - 1;
    } else if (rep->rt == 1) {
        if (++x >= rowcnt[y]) x = 0;
    } else if (rep->up == 1) {
        if (--y < 0) y = colcnt[x] - 1;
    } else if (rep->dw == 1) {
        if (++y >= colcnt[x]) y = 0;
    }
    dt->idx = x + y * 4;
    if (old == dt->idx) return;
    Voice_Call(0x3e9, 0x7f);
    if (dt->pEmbMC[old]) DF_MOBJ_SetDrawEnable(msys_emb_ent(dt->pEmbMC[old])->pSelect, 0);
    if (dt->pEmbMC[dt->idx]) DF_MOBJ_SetDrawEnable(msys_emb_ent(dt->pEmbMC[dt->idx])->pSelect, 1);
}

static void __PlayMcEmbSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    PS2_STACK(struct struct_76b810ec, colour, 0x4c);
    McEmbSys *dt = msys_emb_sys(ioObjp);
    switch (dt->step) {
    case 3:
        ioObjp->stat = 0;
        ioObjp->play_func = (void (*)(void))GFN(__PlayMcEmbSystem);
        ioObjp->count = 0;
        break;
    case 2:
        if (GetPortStatus(dt->mcIdx) == 0 || Mcf_Proc_Ret == -1) {
            if (__SyncDialog(0) == 2) DF_MDIALOG_Close();
            if (__SyncDialog(0) < 4) return;
            dt->dlgMsgId = 0x30e0b;
            dt->step = 0;
            ioObjp->play_func = (void (*)(void))GFN(__DlgMcEmbSystemPull);
            Ac2_Mcf_GetMcInfo();
            return;
        }
        if (CheckMCardProc() != 0) return;
        if (__SyncDialog(0) == 2) {
            DF_MDIALOG_Close();
            Ac2_Mcf_GetMcInfo();
            __MakeMcEmbSystemList(ioObjp);
            dt->step++;
        }
        break;
    case 1: {
        static const int ids[2] = {0x19291, 0x19292};   /* sdata 0x27be10 */
        msys_dlg(5, 0x30e0a, 0, 1);   /* "reading the card" */
        dt->mcInfop->iconp[0] = IconInfo.pEmb;
        dt->mcInfop->iconp[1] = IconInfo.pEmb;
        dt->mcInfop->iconp[2] = IconInfo.pEmb;
        Ac2_Mcf_GetList(dt->mcIdx + 1, Mcf_Start_Info);
        msys_colour(&colour, 0x808c826e);                 /* sdata 0x27be08 */
        dt->pText = DF_MOBJ_NewString(ioObjp, 0, ids[(int)dt->mcIdx], 0, 4, 0x96, 0x60, &colour, 0, 0);
        DF_MOBJ_SetFadeIn(dt->pText);
        dt->step++;
        break;
    }
    case 0:
        ioObjp->count++;
        if (ioObjp->count >= 5) {
            DF_M1HELP_SetStringId(dt->helpId);
            DF_M1HELP_FadeIn();
            dt->step++;
        }
        break;
    }
}

/* real ABI (scripts/protofix.py): t1 = 0 never read, mcInfop in t2, argp in t3, argp2 on the stack. pText / pEmbPC /
 * pEmbMC are not set here: DF_MOBJ_GetNewObject cleared the whole object (0x50 words). */
static struct stMobj *__NewMcEmbSystem(struct stMobj *inObjp, int inPlayer, int inSelect, int inLoadAc, int inMcardIdx,
                                       int unused, struct struct_87202900 *inMcinfop, void *argp, void *argp2)
{
    PS2_FRAME(0x70);
    (void)unused;
    struct stMobj *o = DF_MOBJ_GetNewObject(inObjp);   /* searches after the caller's object */
    if (!o) return 0;
    McEmbSys *dt = msys_emb_sys(o);
    o->type = 0;
    o->stat = 1;
    o->play_func = (void (*)(void))GFN(__PlayMcEmbSystemFadeIn);
    o->count = 0;
    dt->select = inSelect;
    dt->loadAc = inLoadAc;
    dt->player = inPlayer;
    dt->step = 0;
    dt->idx = 0;
    dt->dataNum = 0;
    dt->mcIdx = inMcardIdx;
    dt->mcInfop = inMcinfop;
    dt->argp = argp;
    dt->argp2 = argp2;
    dt->pSave = 0;
    if (inSelect == 3) {
        dt->bSave = 0;
        dt->helpId = 0x192e8;
    } else if (inSelect == 2) {
        dt->bSave = 1;
        dt->helpId = 0x192e7;
    }
    return o;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (9 int args, gen_bridge stops at 8); argp2 is the caller's stack arg at sp+0 */
static void bw____NewMcEmbSystem(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    struct stMobj *r = __NewMcEmbSystem((struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), c->gpr[A1].sw[0],
                                        c->gpr[A2].sw[0], c->gpr[A3].sw[0], c->gpr[T0].sw[0], c->gpr[T1].sw[0],
                                        (struct struct_87202900 *)bridge_ptr(c->gpr[T2].uw[0]),
                                        bridge_ptr(c->gpr[T3].uw[0]), bridge_ptr(rd32(sp)));
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

/* ---- Game data list entries (struct_642efc11 in dt) ---------------------------------------------------------------- */

typedef struct struct_642efc11 GameEnt;    /* one Game data file entry (dt) */
typedef struct struct_acfa1e88 McGameSys;  /* Game data list screen (dt) */

static inline GameEnt *msys_game_ent(struct stMobj *o) { return (GameEnt *)&o->dt; }
static inline McGameSys *msys_game_sys(struct stMobj *o) { return (McGameSys *)&o->dt; }

/* string objects: x / y halfwords of the string dt (obj +0x30 / +0x32) */
static inline short msys_str_x(struct stMobj *s) { return *(short *)((char *)s + 0x30); }
static inline short msys_str_y(struct stMobj *s) { return *(short *)((char *)s + 0x32); }

/* show / hide one entry; select 7 (player data) has the extra credit row. bNew beats bBroken; pCursor only on hide. */
static void __GAMEDATA_Visible(struct stMobj *ioObjp, char select, int bVisible)
{
    PS2_FRAME(0x40);
    if (!ioObjp) return;
    GameEnt *dt = msys_game_ent(ioObjp);
    dt->bVisible = bVisible;
    if (bVisible == 0) {
        if (select == 7) {
            DF_MOBJ_SetDrawEnable(dt->phCredit, bVisible);
            DF_MOBJ_SetDrawEnable(dt->pdCredit, bVisible);
            DF_MOBJ_SetDrawEnable(dt->pC, bVisible);
        }
        DF_MOBJ_SetDrawEnable(dt->pWnd, bVisible);
        DF_MOBJ_SetDrawEnable(dt->pCursor, bVisible);
        DF_MOBJ_SetDrawEnable(dt->phPName, bVisible);
        DF_MOBJ_SetDrawEnable(dt->pdPName, bVisible);
        DF_MOBJ_SetDrawEnable(dt->phPoint, bVisible);
        DF_MOBJ_SetDrawEnable(dt->pdPoint, bVisible);
        DF_MOBJ_SetDrawEnable(dt->pPts, bVisible);
        DF_MOBJ_SetDrawEnable(dt->pInfo, bVisible);
        DF_MOBJ_SetDrawEnable(dt->pNew, bVisible);
        DF_MOBJ_SetDrawEnable(dt->pBroken, bVisible);
        return;
    }
    DF_MOBJ_SetDrawEnable(dt->pWnd, bVisible);
    if (dt->bNew || dt->bBroken) {
        if (select == 7) {
            DF_MOBJ_SetDrawEnable(dt->phCredit, 0);
            DF_MOBJ_SetDrawEnable(dt->pdCredit, 0);
            DF_MOBJ_SetDrawEnable(dt->pC, 0);
        }
        DF_MOBJ_SetDrawEnable(dt->phPName, 0);
        DF_MOBJ_SetDrawEnable(dt->pdPName, 0);
        DF_MOBJ_SetDrawEnable(dt->phPoint, 0);
        DF_MOBJ_SetDrawEnable(dt->pdPoint, 0);
        DF_MOBJ_SetDrawEnable(dt->pPts, 0);
        DF_MOBJ_SetDrawEnable(dt->pInfo, 0);
        if (dt->bNew) {
            DF_MOBJ_SetDrawEnable(dt->pNew, 1);
            DF_MOBJ_SetDrawEnable(dt->pBroken, 0);
        } else {
            DF_MOBJ_SetDrawEnable(dt->pNew, 0);
            DF_MOBJ_SetDrawEnable(dt->pBroken, 1);
        }
    } else {
        if (select == 7) {
            DF_MOBJ_SetDrawEnable(dt->phCredit, 1);
            DF_MOBJ_SetDrawEnable(dt->pdCredit, 1);
            DF_MOBJ_SetDrawEnable(dt->pC, 1);
        }
        DF_MOBJ_SetDrawEnable(dt->phPName, 1);
        DF_MOBJ_SetDrawEnable(dt->pdPName, 1);
        DF_MOBJ_SetDrawEnable(dt->phPoint, 1);
        DF_MOBJ_SetDrawEnable(dt->pdPoint, 1);
        DF_MOBJ_SetDrawEnable(dt->pPts, 1);
        DF_MOBJ_SetDrawEnable(dt->pInfo, dt->info);   /* the stored info flag, not 1 */
        DF_MOBJ_SetDrawEnable(dt->pNew, 0);
        DF_MOBJ_SetDrawEnable(dt->pBroken, 0);
    }
}

static void __GAMEDATA_FadeOut(struct stMobj *ioObjp, char select)
{
    PS2_FRAME(0x30);
    if (!ioObjp) return;
    GameEnt *dt = msys_game_ent(ioObjp);
    if (dt->pWnd) WindowEx_SetFadeOut(dt->pWnd);
    if (dt->pCursor) WindowEx_SetFadeOut(dt->pCursor);
    if (select == 7) {
        DF_MOBJ_SetFadeOut(dt->phCredit);
        DF_MOBJ_SetFadeOut(dt->pdCredit);
        DF_MOBJ_SetFadeOut(dt->pC);
    }
    DF_MOBJ_SetFadeOut(dt->phPName);
    DF_MOBJ_SetFadeOut(dt->pdPName);
    DF_MOBJ_SetFadeOut(dt->phPoint);
    DF_MOBJ_SetFadeOut(dt->pdPoint);
    DF_MOBJ_SetFadeOut(dt->pPts);
    DF_MOBJ_SetFadeOut(dt->pInfo);
    DF_MOBJ_SetFadeOut(dt->pNew);
    DF_MOBJ_SetFadeOut(dt->pBroken);
}

static void __GAMEDATA_FadeIn(struct stMobj *ioObjp, char select)
{
    PS2_FRAME(0x30);
    if (!ioObjp) return;
    GameEnt *dt = msys_game_ent(ioObjp);
    if (dt->pWnd) WindowEx_SetFadeIn(dt->pWnd);
    if (dt->pCursor) WindowEx_SetFadeIn(dt->pCursor);
    if (select == 7) {
        DF_MOBJ_SetFadeIn(dt->phCredit);
        DF_MOBJ_SetFadeIn(dt->pdCredit);
        DF_MOBJ_SetFadeIn(dt->pC);
    }
    DF_MOBJ_SetFadeIn(dt->phPName);
    DF_MOBJ_SetFadeIn(dt->pdPName);
    DF_MOBJ_SetFadeIn(dt->phPoint);
    DF_MOBJ_SetFadeIn(dt->pdPoint);
    DF_MOBJ_SetFadeIn(dt->pPts);
    DF_MOBJ_SetFadeIn(dt->pInfo);
    DF_MOBJ_SetFadeIn(dt->pNew);
    DF_MOBJ_SetFadeIn(dt->pBroken);
}

/* fill one entry (clears bNew / bBroken: callers set them afterwards); the Point sprintf runs for every select */
static void __GAMEDATA_SetData(struct stMobj *ioObjp, char select, char *pName, int Credit, int Point, int bInfo)
{
    PS2_FRAME(0x90);
    PS2_STACK(char[32], buf, 0x70);   /* sprintf destination */
    if (!ioObjp) return;
    GameEnt *dt = msys_game_ent(ioObjp);
    dt->bNew = 0;
    dt->bBroken = 0;
    if (dt->pdPName) DF_MOBJ_SetStringCopy(dt->pdPName, pName);
    if (select == 7) {
        ps2_sprintf_d(buf, msys_fmt_ld, Credit);
        if (dt->pdCredit) DF_MOBJ_SetStringCopy(dt->pdCredit, buf);
    }
    ps2_sprintf_d(buf, msys_fmt_ld, Point);
    if (dt->pdPoint) DF_MOBJ_SetStringCopy(dt->pdPoint, buf);
    dt->info = bInfo;
    __GAMEDATA_Visible(ioObjp, select, dt->bVisible);
}

/* scroll one entry by Num rows (55 px each); a row leaving the window is only faded (no Visible(0)) */
static void __GAMEDATA_Move(struct stMobj *ioObjp, char select, int Num)
{
    PS2_FRAME(0x50);
    if (!ioObjp) return;
    GameEnt *dt = msys_game_ent(ioObjp);
    int oldvis = (0 <= dt->pos && dt->pos < 6);
    dt->pos += Num;
    MoverI_Move(&dt->Move, 55 * dt->pos, 5);
    int newvis = (0 <= dt->pos && dt->pos < 6);
    if (oldvis == newvis) return;
    if (newvis) {
        __GAMEDATA_Visible(ioObjp, select, 1);
        __GAMEDATA_FadeIn(ioObjp, select);
    } else {
        __GAMEDATA_FadeOut(ioObjp, select);
    }
}

/* play_func of every entry: move all parts to their base y + the mover's current offset */
static void __GAMEDATA_Update(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    PS2_STACK(struct stMenuRect, rect, 0x30);
    if (!ioObjp) return;
    GameEnt *dt = msys_game_ent(ioObjp);
    MoverI_Update(&dt->Move);
    if (dt->pWnd) {
        WindowEx_GetRect(dt->pWnd, &rect);
        rect.y = dt->y_wnd + dt->Move.Cur;
        WindowEx_Move(dt->pWnd, &rect, 0);
    }
    if (dt->pCursor) {
        WindowEx_GetRect(dt->pCursor, &rect);
        rect.y = dt->y_cur + dt->Move.Cur;
        WindowEx_Move(dt->pCursor, &rect, 0);
    }
    if (dt->phPName) DF_MOBJ_SetStringPos(dt->phPName, msys_str_x(dt->phPName), dt->y_name[0] + dt->Move.Cur);
    if (dt->pdPName) DF_MOBJ_SetStringPos(dt->pdPName, msys_str_x(dt->pdPName), dt->y_name[1] + dt->Move.Cur);
    if (dt->select == 7) {
        if (dt->phCredit) DF_MOBJ_SetStringPos(dt->phCredit, msys_str_x(dt->phCredit), dt->y_credit[0] + dt->Move.Cur);
        if (dt->pdCredit) DF_MOBJ_SetStringPos(dt->pdCredit, msys_str_x(dt->pdCredit), dt->y_credit[1] + dt->Move.Cur);
        if (dt->pC) DF_MOBJ_SetStringPos(dt->pC, msys_str_x(dt->pC), dt->y_credit[2] + dt->Move.Cur);
    }
    if (dt->phPoint) DF_MOBJ_SetStringPos(dt->phPoint, msys_str_x(dt->phPoint), dt->y_point[0] + dt->Move.Cur);
    if (dt->pdPoint) DF_MOBJ_SetStringPos(dt->pdPoint, msys_str_x(dt->pdPoint), dt->y_point[1] + dt->Move.Cur);
    if (dt->pPts) DF_MOBJ_SetStringPos(dt->pPts, msys_str_x(dt->pPts), dt->y_point[2] + dt->Move.Cur);
    if (dt->pInfo) *(int *)((char *)dt->pInfo + 0x14) = dt->y_info + dt->Move.Cur;   /* sprite: raw y store */
    if (dt->pNew) DF_MOBJ_SetStringPos(dt->pNew, msys_str_x(dt->pNew), dt->y_text + dt->Move.Cur);
    if (dt->pBroken) DF_MOBJ_SetStringPos(dt->pBroken, msys_str_x(dt->pBroken), dt->y_text + dt->Move.Cur);
}

static inline void msys_game_erase(struct stMobj **pp)
{
    if (*pp) {
        msys_erase(*pp);
        *pp = 0;
    }
}

static void __GAMEDATA_Delete(struct stMobj *ioObjp, char select)
{
    PS2_FRAME(0x40);
    if (!ioObjp) return;
    GameEnt *dt = msys_game_ent(ioObjp);
    if (dt->pWnd) WindowEx_Delete(dt->pWnd);
    dt->pWnd = 0;
    if (dt->pCursor) WindowEx_Delete(dt->pCursor);
    dt->pCursor = 0;
    if (select == 7) {
        msys_game_erase(&dt->phCredit);
        msys_game_erase(&dt->pdCredit);
        msys_game_erase(&dt->pC);
    }
    msys_game_erase(&dt->phPName);
    msys_game_erase(&dt->pdPName);
    msys_game_erase(&dt->phPoint);
    msys_game_erase(&dt->pdPoint);
    msys_game_erase(&dt->pPts);
    msys_game_erase(&dt->pInfo);
    msys_game_erase(&dt->pNew);
    msys_game_erase(&dt->pBroken);
    msys_erase(ioObjp);
}

/* new entry (pool search from inObjp + 1); bNew starts at 1 and is cleared by SetData */
static struct stMobj *__GAMEDATA_New(struct stMobj *inObjp, char select, int No, int Pos)
{
    PS2_FRAME(0x120);
    PS2_STACK(struct stMenuRect, wrect, 0x90);              /* GetRect out (pWnd) */
    PS2_STACK(struct _LAYOUT_RET_DATA, lo, 0xa0);           /* "info_0" sprite layout */
    PS2_STACK(struct stMenuRect, rect, 0xf0);               /* cursor window rect */
    PS2_STACK(struct struct_76b810ec[2], colpair, 0x108);   /* window colour {0x30808080, 0x80808080} */
    PS2_STACK(struct struct_76b810ec, colD, 0x110);         /* cursor window */
    PS2_STACK(struct struct_76b810ec, colC, 0x114);         /* "Corrupt File" */
    PS2_STACK(struct struct_76b810ec, colB, 0x118);         /* data strings */
    PS2_STACK(struct struct_76b810ec, colA, 0x11c);         /* headings */
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    GameEnt *dt = msys_game_ent(obj);
    obj->type = 0;
    obj->stat = 0;
    obj->play_func = (void (*)(void))GFN(__GAMEDATA_Update);
    dt->no = No;
    dt->pos = Pos;
    dt->bNew = 1;
    dt->bBroken = 0;
    dt->select = select;
    MoverI_Init(&dt->Move);
    MoverI_SetValue(&dt->Move, 55 * Pos);
    msys_colour(&colA, 0x8089bbda);
    msys_colour(&colB, 0x80909090);
    msys_colour(&colC, 0x80000080);
    msys_colour(&colD, 0x80808080);
    rect.x = 0x30;
    rect.y = 0x40;
    rect.w = 0x220;
    rect.h = 0x2e;
    char *layout = (char *)LAYOUT_GetLayoutData(0x462);
    if (select == 7) layout = (char *)LAYOUT_GetLayoutData(0x416);
    dt->pWnd = WindowEx_NewFromLayout(obj, (_WINDOWEX_TYPE)0, layout, (char *)"ita_0");
    if (dt->pWnd) {
        WindowEx_GetRect(dt->pWnd, &wrect);
        dt->y_wnd = (short)wrect.y;
    }
    dt->pCursor = WindowEx_New(obj, (_WINDOWEX_TYPE)0x25, &rect, &colD);
    if (dt->pCursor) {
        WindowEx_SetAlphaReg(dt->pCursor, 0x48);
        WindowEx_BlinkBegin(dt->pCursor);
        DF_MOBJ_SetDrawEnable(dt->pCursor, 0);
        dt->y_cur = (short)rect.y;
    }
    msys_colour(&colpair[0], 0x30808080);
    msys_colour(&colpair[1], 0x80808080);
    if (dt->pWnd) WindowEx_SetColor(dt->pWnd, &colpair[0]);
    DF_MOBJ_SetDrawEnable(dt->pCursor, 0);

    dt->phPName = DF_MOBJ_NewStringFromLayoutName(obj, 0, 0x19294, 0, layout, (char *)"koumoku_0", &colA, 0, 0);
    if (dt->phPName) dt->y_name[0] = msys_str_y(dt->phPName);
    dt->pdPName = DF_MOBJ_NewStringBaseFromLayoutName(obj, layout, (char *)"name_0", &colB, 0, 0);
    if (dt->pdPName) dt->y_name[1] = msys_str_y(dt->pdPName);
    if (select == 7) {
        dt->phCredit = DF_MOBJ_NewStringFromLayoutName(obj, 0, 0x1925d, 0, layout, (char *)"koumoku_6", &colA, 0, 0);
        if (dt->phCredit) dt->y_credit[0] = msys_str_y(dt->phCredit);
        dt->pdCredit = DF_MOBJ_NewStringBaseFromLayoutName(obj, layout, (char *)"suuchi_0", &colB, 0, 0x10000);
        if (dt->pdCredit) dt->y_credit[1] = msys_str_y(dt->pdCredit);
        dt->pC = DF_MOBJ_NewStringFromLayoutName(obj, 0, 0x186c4, 0, layout, (char *)"kigou_0", &colB, 0, 0);
        if (dt->pC) dt->y_credit[2] = msys_str_y(dt->pC);
    }
    dt->phPoint = DF_MOBJ_NewStringFromLayoutName(obj, 0, select == 7 ? 0x1929f : 0x192a0, 0, layout,
                                                  (char *)"koumoku_7", &colA, 0, 0);
    if (dt->phPoint) dt->y_point[0] = msys_str_y(dt->phPoint);
    dt->pdPoint = DF_MOBJ_NewStringBaseFromLayoutName(obj, layout, (char *)"suuchi_1", &colB, 0, 0x10000);
    if (dt->pdPoint) dt->y_point[1] = msys_str_y(dt->pdPoint);
    dt->pPts = DF_MOBJ_NewStringFromLayoutName(obj, 0, 0x1929e, 0, layout, (char *)"kigou_1", &colB, 0, 0);
    if (dt->pPts) dt->y_point[2] = msys_str_y(dt->pPts);

    DF_LO_GetLayoutData((unsigned char *)layout, (unsigned char *)"info_0", 0, &lo);
    lo.Pos.x += 0x28;
    dt->pInfo = DF_MOBJ_NewSpriteFromLayout(obj, &lo);
    dt->y_info = (short)lo.Pos.y;   /* re-read from the stack struct after the call */

    dt->pNew = DF_MOBJ_NewString(obj, 0, 0x192e3, 0, 3, (select == 7 ? 0 : 0x4e) + 0x3c, 0x4a, &colB, 0, 0);
    dt->pBroken = DF_MOBJ_NewStringDirect(obj, msys_corrupt, 3, (select == 7 ? 0 : 0x4e) + 0x3c, 0x4a, &colC, 0, 0);
    dt->y_text = 0x4a;
    __GAMEDATA_Visible(obj, select, 0 <= Pos && Pos < 6);
    return obj;
}

/* ---- Game data list screen (struct_acfa1e88 in dt) ----------------------------------------------------------------- */

/* start the list fade-out (6 call sites: Load / Save / DlgPull / __PlayMcGameSystem) */
static void __SetMcGameSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    McGameSys *dt = msys_game_sys(ioObjp);
    DF_MOBJ_SetFadeOut(dt->pText);
    if (dt->pUp) DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pUp);
    if (dt->pDown) DF_MOBJ2_SetBlinkSpriteFadeOut(dt->pDown);
    for (int i = 0; i < 8; i++) __GAMEDATA_FadeOut(dt->pData[i], dt->select);
    DF_M1HELP_FadeOut();
    Voice_Call(0x3ed, 0x7f);
    dt->step = 0;
    ioObjp->count = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = (void (*)(void))GFN(__PlayMcGameSystemFadeOut);
}

/* "memory card pulled" dialog, then fade the list out (no DF_MDIALOG_Close) */
static void __DlgMcGameSystemPull(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    McGameSys *dt = msys_game_sys(ioObjp);
    if (dt->step == 1) {
        if (__SyncDialog(0) >= 4) {
            SetForceMCardStatus(dt->mcIdx, 0);
            __SetMcGameSystemFadeOut(ioObjp);
        }
    } else if (dt->step == 0) {
        msys_dlg(5, dt->dlgMsgId, 0, 0);
        dt->step++;
    }
}

/* move the cursor from entry prev to next (no range tests) and scroll the list so next is in the 6-row window */
static void __SelectMcGameSystem(struct stMobj *ioObjp, int prev, int next)
{
    PS2_FRAME(0x60);
    PS2_STACK(struct struct_76b810ec[2], cn, 0x50);   /* next uses its second word */
    PS2_STACK(struct struct_76b810ec[2], cp, 0x58);   /* prev uses its first word */
    McGameSys *dt = msys_game_sys(ioObjp);
    struct stMobj *e = dt->pData[prev];
    msys_colour(&cp[0], 0x30808080);
    msys_colour(&cp[1], 0x80808080);
    if (e) {
        GameEnt *d = msys_game_ent(e);
        if (d->pWnd) WindowEx_SetColor(d->pWnd, &cp[0]);
        DF_MOBJ_SetDrawEnable(d->pCursor, 0);
    }
    e = dt->pData[next];
    msys_colour(&cn[0], 0x30808080);
    msys_colour(&cn[1], 0x80808080);
    if (e) {
        GameEnt *d = msys_game_ent(e);
        if (d->pWnd) WindowEx_SetColor(d->pWnd, &cn[1]);
        DF_MOBJ_SetDrawEnable(d->pCursor, 1);
    }
    if (dt->dataNum < 7) return;
    int top = dt->top;
    if (next < top) {
        int d = top - next;
        dt->top = top - d;
        for (int i = 0; i < 8; i++) __GAMEDATA_Move(dt->pData[i], dt->select, d);
    } else if (top + 5 < next) {
        int d = next - (top + 5);
        dt->top = top + d;
        for (int i = 0; i < 8; i++) __GAMEDATA_Move(dt->pData[i], dt->select, -d);
    }
    if (dt->pUp) DF_MOBJ_SetDrawEnable(dt->pUp, dt->top != 0);
    if (dt->pDown) DF_MOBJ_SetDrawEnable(dt->pDown, dt->top < dt->dataNum - 6);
}

/* ---- group 4: Load / Save, list build ---- */

/* inlined exit of Load / Save (two byte-identical copies each): result code when done, then always the fade-out */
static void msys_game_exit(struct stMobj *ioObjp)
{
    McGameSys *dt = msys_game_sys(ioObjp);
    if (dt->stat >= 2) {
        if (dt->bSave) {
            if (dt->select == 0) s_msysMcResult = 1;
            if (dt->select == 6) s_msysMcResult = 1;
        } else {
            s_msysMcResult = 2;
            MenuMng.stat = 4;
        }
    }
    __SetMcGameSystemFadeOut(ioObjp);
}

/* card pulled / engine error (top of Load / Save) */
static void msys_game_pulled(struct stMobj *ioObjp, int msgWorking)
{
    McGameSys *dt = msys_game_sys(ioObjp);
    if (__SyncDialog(0) == 2) DF_MDIALOG_Close();
    if (__SyncDialog(0) < 4) return;
    if (dt->stat >= 2) {
        msys_game_exit(ioObjp);
        return;
    }
    dt->dlgMsgId = dt->stat == 1 ? msgWorking : 0x30e0b;
    dt->step = 0;
    ioObjp->play_func = (void (*)(void))GFN(__DlgMcGameSystemPull);
    Ac2_Mcf_GetMcInfo();
}

/* stopover flag of a save: Game_IsStopoverSave only in gMcMode 1 */
static int msys_game_is_stop(McGameSys *dt)
{
    if (gMcMode == 1) return Game_IsStopoverSave(dt->player) != 0;
    return dt->select == 6;
}

static inline void msys_game_back(struct stMobj *ioObjp)
{
    msys_game_sys(ioObjp)->step = 0;
    ioObjp->play_func = (void (*)(void))GFN(__PlayMcGameSystem);
    ioObjp->count = 0;
}

static void __LoadMcGameSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    PS2_STACK(int, result, 0x5c);
    McGameSys *dt = msys_game_sys(ioObjp);
    int mcIdx = dt->mcIdx;
    if (GetPortStatus(mcIdx) == 0 || Mcf_Proc_Ret == -1) {
        msys_game_pulled(ioObjp, 0x30e18);
        return;
    }
    struct stMobj *e;
    switch (dt->step) {
    case 0:
        dt->stat = 0;
        e = dt->pData[dt->idx];
        if (e && msys_game_ent(e)->bBroken) {
            msys_dlg(5, 0x30e41, 0, 0);
            dt->step++;
        } else {
            dt->step += 2;
        }
        break;
    case 1:
        if (__SyncDialog(0) >= 4) msys_game_back(ioObjp);
        break;
    case 2:
        msys_dlg(2, 0x30e16, 1, 0);
        dt->step++;
        break;
    case 3:
        if (__SyncDialog(&result) >= 4) {
            if (result == 1) dt->step++;
            else msys_game_back(ioObjp);
        }
        break;
    case 4: {
        dt->stat = 1;
        int size = dt->select == 7 ? 0xd800 : 0x1e000;
        if (!dt->pSave) dt->pSave = DF_MMM_Alloc(0, size);
        ZeroMemory32b(dt->pSave, size / 4 / 4);   /* counts words: clears only a quarter (original bug) */
        dt->mcInfop->datap = dt->pSave;
        e = dt->pData[dt->idx];
        result = Ac2_Mcf_Load(mcIdx + 1, e ? msys_game_ent(e)->no : -1);
        if (result == -1) {
            dt->step = 0x5a;
        } else {
            msys_dlg(5, 0x30e17, 0, 1);
            ioObjp->count = 0;
            dt->step++;
        }
        break;
    }
    case 5:
        ioObjp->count++;
        if (Mcf_Proc_Ret != 0) {
            if (Mcf_Proc_Ret == -1) dt->step = 0x5a;
            else dt->step++;
        }
        break;
    case 6:
        ioObjp->count++;
        if (ioObjp->count < 15) break;
        if (__SyncDialog(0) == 2) DF_MDIALOG_Close();
        if (__SyncDialog(0) >= 4) {
            msys_dlg(4, 0x30e19, 0, 1);
            if (dt->select == 7) {
                /* title load: player data + emblem straight into slot 0 */
                FlushCache(0);
                char *ps = (char *)dt->pSave;
                PlayerInfo_Load(0, (struct StPlayerInfo *)(ps + 0x4990));
                PlayerInfo_InitPlayer(1);
                PlayerInfo_InitPlayer(2);
                PlayerInfo_InitPlayer(3);
                CopyMemory8b(EmblemTM2[0], ps + 0x9190, 0x4420);
                CopyMemory8b(&g_gamedata[0], ps, 0x30);
                CopyMemory8b(&g_gamedata[0].option, ps + 0x50, 0x80);
                g_gamedata[0].cockpit = *(unsigned int *)(ps + 0x38);
                g_gamedata[0].cockpitcol = *(char *)(ps + 0x3c);
                FlushCache(0);
            } else {
                Save_DistributeSaveData((struct StAcrtSaveData *)dt->pSave, dt->player);   /* game_opt unlock_parts hook */
            }
            gLoadCheckF[(int)dt->player] = 1;
            DF_M_AC_SetAll();              /* asm leaves a0 = 1: the native takes no argument */
            DF_MPART_ExtPartsDispose();    /* asm leaves a0 = player: likewise */
            DF_MGARAGE_SetMissionAc(dt->player);
            if (gMcMode != 1) {
                MenuMng.mc_slot = dt->mcIdx;
                MenuMng.mc_file = (char)dt->idx;
            }
            dt->stat = 2;
            dt->step++;
        }
        break;
    case 7:
        ioObjp->count++;
        if (ioObjp->count >= 15 && __SyncDialog(0) == 2) dt->step++;
        break;
    case 8:
        msys_pad_close(&dt->step);
        break;
    case 9:
        if (__SyncDialog(0) >= 4) dt->step = 0x64;
        break;
    case 0x5a:
        if (__SyncDialog(0) == 2) DF_MDIALOG_Close();
        if (__SyncDialog(0) >= 4) {
            msys_dlg(5, 0x30e18, 0, 1);
            ioObjp->count = 0;
            dt->step++;
        }
        break;
    case 0x5b:
        ioObjp->count++;
        if (ioObjp->count >= 15 && __SyncDialog(0) == 2) dt->step++;
        break;
    case 0x5c:
        msys_pad_close(&dt->step);
        break;
    case 0x5d:
        if (__SyncDialog(0) >= 4) {
            Ac2_Mcf_GetMcInfo();
            Ac2_Mcf_Proc(0);
            dt->step = 0x64;
        }
        break;
    default:   /* 0x64 and unknown steps */
        msys_game_exit(ioObjp);
        break;
    }
}

static void __SaveMcGameSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    PS2_STACK(int, result, 0x5c);
    McGameSys *dt = msys_game_sys(ioObjp);
    int mcIdx = dt->mcIdx;
    if (GetPortStatus(mcIdx) == 0 || Mcf_Proc_Ret == -1) {
        msys_game_pulled(ioObjp, 0x30e13);
        return;
    }
    struct stMobj *e;
    switch (dt->step) {
    case 0:
        dt->stat = 0;
        e = dt->pData[dt->idx];
        if (e && msys_game_ent(e)->bNew && (unsigned int)dt->mcInfop->free_size < 0xa0) {
            msys_dlg(5, 0x30e15, 0, 0);   /* not enough free space for a new file */
            dt->step++;
        } else {
            dt->step += 2;
        }
        break;
    case 1:
        if (__SyncDialog(0) >= 4) msys_game_back(ioObjp);
        break;
    case 2:
        e = dt->pData[dt->idx];
        msys_dlg(2, e && msys_game_ent(e)->bNew ? 0x30e10 : 0x30e11, 1, 0);
        dt->step++;
        break;
    case 3:
        if (__SyncDialog(&result) >= 4) {
            if (result == 1) dt->step++;
            else msys_game_back(ioObjp);
        }
        break;
    case 4:
        dt->stat = 1;
        if (!dt->pSave) dt->pSave = DF_MMM_Alloc(0, 0x1e000);
        ZeroMemory32b(dt->pSave, 0x7800);
        dt->mcInfop->datap = dt->pSave;
        Save_MakeSaveData((struct StAcrtSaveData *)dt->pSave, dt->player, msys_game_is_stop(dt));
        e = dt->pData[dt->idx];
        /* an existing "new" slot or an empty list saves as a new file */
        result = Ac2_Mcf_Save(mcIdx + 1, e && !msys_game_ent(e)->bNew ? msys_game_ent(e)->no : -1);
        if (result == -1) {
            dt->step = 0x5a;
        } else {
            msys_dlg(5, 0x30e12, 0, 1);
            ioObjp->count = 0;
            dt->step++;
        }
        break;
    case 5:
        ioObjp->count++;
        if (Mcf_Proc_Ret != 0) {
            if (Mcf_Proc_Ret == -1) dt->step = 0x5a;
            else dt->step++;
        }
        break;
    case 6:
        ioObjp->count++;
        if (ioObjp->count < 15) break;
        if (__SyncDialog(0) == 2) DF_MDIALOG_Close();
        if (__SyncDialog(0) >= 4) {
            msys_dlg(4, 0x30e14, 0, 1);
            char *name = Game_GetName(dt->player);
            int money = Game_GetMoney(dt->player);
            int point = g_gamedata[(int)dt->player].pointTotal;
            int isStop = msys_game_is_stop(dt);
            __GAMEDATA_SetData(dt->pData[dt->idx], dt->select, name, money, point, isStop);
            if (gMcMode != 1) {
                MenuMng.mc_slot = dt->mcIdx;
                MenuMng.mc_file = (char)dt->idx;
            }
            dt->stat = 2;
            dt->step++;
        }
        break;
    case 7:
        ioObjp->count++;
        if (ioObjp->count >= 15 && __SyncDialog(0) == 2) dt->step++;
        break;
    case 8:
        msys_pad_close(&dt->step);
        break;
    case 9:
        if (__SyncDialog(0) >= 4) dt->step = 0x64;
        break;
    case 0x5a:
        if (__SyncDialog(0) == 2) DF_MDIALOG_Close();
        if (__SyncDialog(0) >= 4) {
            msys_dlg(5, 0x30e13, 0, 1);
            ioObjp->count = 0;
            dt->step++;
        }
        break;
    case 0x5b:
        ioObjp->count++;
        if (ioObjp->count >= 15 && __SyncDialog(0) == 2) dt->step++;
        break;
    case 0x5c:
        msys_pad_close(&dt->step);
        break;
    case 0x5d:
        if (__SyncDialog(0) >= 4) {
            Ac2_Mcf_GetMcInfo();
            Ac2_Mcf_Proc(0);
            dt->step = 0x64;
        }
        break;
    default:   /* 0x64 and unknown steps */
        msys_game_exit(ioObjp);
        break;
    }
}

/* build the Game data list from the card's file list (+ one "new file" slot when saving), scroll arrows past 6 rows */
static void __MakeMcGameSystemList(struct stMobj *ioObjp)
{
    PS2_FRAME(0xc0);
    McGameSys *dt = msys_game_sys(ioObjp);
    int nlist[8] = {0, 1, 2, 3, 4, 5, 6, 7};   /* OLMENU 0x1ee0220 */
    int fnum = 0;
    int mcIdx = dt->mcIdx;
    struct struct_17022ecb *info = mc_info[mcIdx];
    for (int i = 0; i < 8; i++, info++) {
        int r = Ac2_Mcf_GetListInfo(info, mcIdx + 1, i);
        if (r == -1) continue;
        if (r == -2) {
            /* broken file */
            struct stMobj *e = dt->pData[fnum] = __GAMEDATA_New(ioObjp, dt->select, i, fnum);
            if (e) {
                GameEnt *d = msys_game_ent(e);
                d->bNew = 0;
                d->bBroken = 1;
                __GAMEDATA_Visible(e, dt->select, d->bVisible);
            }
            __GAMEDATA_FadeIn(dt->pData[fnum], dt->select);
        } else {
            struct stMobj *e = dt->pData[fnum] = __GAMEDATA_New(ioObjp, dt->select, i, fnum);
            __GAMEDATA_SetData(e, dt->select, info->name, info->money, info->point, info->info);
            __GAMEDATA_FadeIn(dt->pData[fnum], dt->select);
        }
        nlist[i] = -1;
        fnum++;
    }
    if (dt->bSave && fnum < 8) {
        int j = 0;
        while (j < 8 && nlist[j] == -1) j++;
        if (j < 8) {
            dt->pData[fnum] = __GAMEDATA_New(ioObjp, dt->select, nlist[j], fnum);
            __GAMEDATA_FadeIn(dt->pData[fnum], dt->select);
            fnum++;
        }
    }
    dt->dataNum = fnum;
    if (dt->idx < 0 || dt->idx >= dt->dataNum) dt->idx = 0;
    if (dt->dataNum >= 7) {
        dt->pUp = DF_MOBJ2_NewBlinkSpriteFromLayoutName(ioObjp, (char *)LAYOUT_GetLayoutData(0x416), msys_up_0, 0, 0);
        if (dt->pUp) {
            DF_MOBJ2_SetBlinkSpriteFadeIn(dt->pUp);
            DF_MOBJ_SetDrawEnable(dt->pUp, 0);
        }
        dt->pDown = DF_MOBJ2_NewBlinkSpriteFromLayoutName(ioObjp, (char *)LAYOUT_GetLayoutData(0x416), msys_down_0, 0, 0);
        if (dt->pDown) {
            DF_MOBJ2_SetBlinkSpriteFadeIn(dt->pDown);
            DF_MOBJ_SetDrawEnable(dt->pDown, 0);
        }
    }
    __SelectMcGameSystem(ioObjp, dt->idx, dt->idx);
    Voice_Call(0x3ec, 0x7f);
}

/* fade-out of the Game data list: wait 5 frames, free the save buffer and every widget, then die (step 1) */
static void __PlayMcGameSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    McGameSys *dt = msys_game_sys(ioObjp);
    if (dt->step == 1) {
        ioObjp->stat = 5;
        ioObjp->play_func = 0;
    } else if (dt->step == 0) {
        ioObjp->count++;
        if (ioObjp->count < 5) return;
        if (dt->pSave) DF_MMM_Free(0, dt->pSave);
        dt->pSave = 0;
        if (dt->pText) { msys_erase(dt->pText); dt->pText = 0; }
        if (dt->pUp) { msys_erase(dt->pUp); dt->pUp = 0; }
        if (dt->pDown) { msys_erase(dt->pDown); dt->pDown = 0; }
        for (int i = 0; i < 8; i++) {
            __GAMEDATA_Delete(dt->pData[i], dt->select);
            dt->pData[i] = 0;
        }
        dt->step++;
    }
}

/* idle state of the Game data list: card check first (unlike the Emb twin), key guide, cancel / decide, up / down
 * cursor with wrap */
static void __PlayMcGameSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    McGameSys *dt = msys_game_sys(ioObjp);
    if (GetPortStatus(dt->mcIdx) == 0 || Mcf_Proc_Ret == -1) {
        MenuKeyGuide_Hide();
        dt->dlgMsgId = 0x30e0b;
        dt->step = 0;
        ioObjp->play_func = (void (*)(void))GFN(__DlgMcGameSystemPull);
        Ac2_Mcf_GetMcInfo();
        return;
    }
    if (MenuKeyGuide_IsShown()) return;
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    if (pad->start == 1) {
        MenuKeyGuide_Show(dt->bSave ? 8 : 9);
        return;
    }
    if (pad->Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        __SetMcGameSystemFadeOut(ioObjp);
        return;
    }
    int n = dt->dataNum;
    if (n <= 0) return;
    if (pad->Rdw == 1) {
        Voice_Call(0x3ea, 0x7f);
        dt->stat = 0;
        dt->step = 0;
        ioObjp->count = 0;
        if (dt->bSave) ioObjp->play_func = (void (*)(void))GFN(__SaveMcGameSystem);
        else ioObjp->play_func = (void (*)(void))GFN(__LoadMcGameSystem);
        return;
    }
    if (n < 2) return;
    int old = dt->idx;
    int chg = 0;
    if (pad->repeat.dw) {
        chg = 1;
        dt->idx = old + 1;
        if (!(old + 1 < n)) dt->idx = 0;
    } else if (pad->repeat.up) {
        chg = 1;
        dt->idx = old - 1;
        if (old - 1 < 0) dt->idx = dt->dataNum - 1;
    }
    if (chg) {
        Voice_Call(0x3e9, 0x7f);
        __SelectMcGameSystem(ioObjp, old, dt->idx);
    }
}

static void __PlayMcGameSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    PS2_STACK(int[2], strno, 0x40);
    PS2_STACK(struct struct_76b810ec, col, 0x4c);
    McGameSys *dt = msys_game_sys(ioObjp);
    switch (dt->step) {
    case 3:
        ioObjp->stat = 0;
        ioObjp->play_func = (void (*)(void))GFN(__PlayMcGameSystem);
        ioObjp->count = 0;
        break;
    case 2:
        if (GetPortStatus(dt->mcIdx) == 0 || Mcf_Proc_Ret == -1) {
            if (__SyncDialog(0) == 2) DF_MDIALOG_Close();
            if (__SyncDialog(0) < 4) return;
            dt->dlgMsgId = 0x30e0b;
            dt->step = 0;
            ioObjp->play_func = (void (*)(void))GFN(__DlgMcGameSystemPull);
            Ac2_Mcf_GetMcInfo();
            return;
        }
        if (CheckMCardProc() != 0) return;
        if (__SyncDialog(0) == 2) {
            DF_MDIALOG_Close();
            Ac2_Mcf_GetMcInfo();
            __MakeMcGameSystemList(ioObjp);
            dt->step++;
        }
        break;
    case 1:
        msys_dlg(5, 0x30e0a, 0, 1);   /* "reading the card" */
        dt->mcInfop->iconp[0] = IconInfo.pGame;
        dt->mcInfop->iconp[1] = IconInfo.pGame;
        dt->mcInfop->iconp[2] = IconInfo.pGame;
        Ac2_Mcf_GetList(dt->mcIdx + 1, Mcf_Start_Info);
        msys_colour(&col, 0x808c826e);   /* sdata 0x27bde4 */
        strno[0] = 0x19291;              /* sdata 0x27bde8 */
        strno[1] = 0x19292;
        dt->pText = DF_MOBJ_NewString(ioObjp, 0, strno[(int)dt->mcIdx], 0, 4, 0x2f, 0x28, &col, 0, 0);
        DF_MOBJ_SetFadeIn(dt->pText);
        dt->step++;
        break;
    case 0:
        ioObjp->count++;
        if (ioObjp->count >= 5) {
            DF_M1HELP_SetStringId(dt->helpId);
            DF_M1HELP_FadeIn();
            dt->step++;
        }
        break;
    }
}

/* real ABI (scripts/protofix.py): t1 = 0 never read, mcInfop in t2, argp in t3, argp2 on the stack. pData[] / pUp /
 * pDown / pText stay 0 from DF_MOBJ_GetNewObject; bSave / helpId stay 0 for selects other than 0, 1, 6, 7. */
static struct stMobj *__NewMcGameSystem(struct stMobj *inObjp, int inPlayer, int inSelect, int inLoadAc, int inMcardIdx,
                                        int unused, struct struct_87202900 *inMcinfop, void *argp, void *argp2)
{
    PS2_FRAME(0x70);
    (void)unused;
    struct stMobj *o = DF_MOBJ_GetNewObject(inObjp);
    if (!o) return 0;
    McGameSys *dt = msys_game_sys(o);
    o->type = 0;
    o->stat = 1;
    o->play_func = (void (*)(void))GFN(__PlayMcGameSystemFadeIn);
    o->count = 0;
    dt->select = inSelect;
    dt->loadAc = inLoadAc;
    dt->player = inPlayer;
    dt->step = 0;
    dt->idx = gMcMode == 1 ? 0 : MenuMng.mc_file;
    dt->dataNum = 0;
    dt->top = 0;
    dt->mcIdx = inMcardIdx;
    dt->mcInfop = inMcinfop;
    dt->argp = argp;
    dt->argp2 = argp2;
    dt->pSave = 0;
    int ref = gMcMode == 1 ? 0 : (signed char)MenuMng.mc_slot;
    if (dt->mcIdx != ref) dt->idx = 0;
    if (inSelect == 7) {
        dt->bSave = 0;
        dt->helpId = 0x192f1;
    } else if (inSelect == 6) {
        dt->bSave = 1;
        dt->helpId = 0x192e4;
    } else if (inSelect == 1) {
        dt->bSave = 0;
        dt->helpId = 0x192e6;
    } else if (inSelect == 0) {
        dt->bSave = 1;
        dt->helpId = 0x192e5;
    }
    return o;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (9 int args, gen_bridge stops at 8); argp2 is the caller's stack arg at sp+0 */
static void bw____NewMcGameSystem(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    struct stMobj *r = __NewMcGameSystem((struct stMobj *)bridge_ptr(c->gpr[A0].uw[0]), c->gpr[A1].sw[0],
                                         c->gpr[A2].sw[0], c->gpr[A3].sw[0], c->gpr[T0].sw[0], c->gpr[T1].sw[0],
                                         (struct struct_87202900 *)bridge_ptr(c->gpr[T2].uw[0]),
                                         bridge_ptr(c->gpr[T3].uw[0]), bridge_ptr(rd32(sp)));
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

/* ---- MC_MODEL: the two memory-card models + labels of the MC shell (struct_fac124a6 in dt) -------------------------- */

typedef struct struct_fac124a6 McModel;
typedef struct struct_5ef11617 McShell;   /* MC system shell (dt) */

static inline McModel *msys_mc_model(struct stMobj *o) { return (McModel *)&o->dt; }
static inline McShell *msys_mc_shell(struct stMobj *o) { return (McShell *)&o->dt; }

/* MWCC float -> unsigned conversion (cvt.w.s with the 2^31 split) */
static inline unsigned int msys_ftou(float f)
{
    if (f >= 2147483648.0f) return (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u;
    return (unsigned int)ps2_ftoi(f);
}

/* play_func: card present / absent fade (ExistAlpha), label colours and model alphas from the three movers */
static void __MC_MODEL_Update(struct stMobj *ioObjp)
{
    PS2_FRAME(0x70);
    PS2_STACK(struct struct_76b810ec, col_b, 0x68);   /* to interpreted DF_MMODEL_SetVertColor */
    PS2_STACK(struct struct_76b810ec, col_a, 0x6c);
    McModel *dt = msys_mc_model(ioObjp);
    if (!dt->bDraw) return;
    if (GetPortStatus(dt->slotNum) == 2) {
        if (!dt->bExist) {
            dt->bExist = 1;
            MoverI_SetValue(&dt->ExistAlpha, 0);
            MoverI_Move(&dt->ExistAlpha, 0x80, 5);
        }
    } else if (dt->bExist) {
        dt->bExist = 0;
        MoverI_SetValue(&dt->ExistAlpha, 0x80);
        MoverI_Move(&dt->ExistAlpha, 0, 5);
    }
    MoverI_Update(&dt->ExistAlpha);
    MoverI_Update(&dt->SelAlpha);
    MoverI_Update(&dt->SceneAlpha);
    msys_colour(&col_a, 0x408c826e);   /* sdata 0x27bde0 */
    float sceneA = (float)dt->SceneAlpha.Cur / 128.0f;
    float existA = (float)dt->ExistAlpha.Cur / 128.0f;
    float notEx = 1.0f - existA;
    col_a.a = (unsigned char)msys_ftou((float)(dt->SelAlpha.Cur + 0x40) * sceneA);
    if (dt->pName[0]) DF_MOBJ_SetStringColor(dt->pName[0], &col_a);
    if (dt->pName[1]) DF_MOBJ_SetStringColor(dt->pName[1], &col_a);
    col_a.a = (unsigned char)msys_ftou((float)dt->SceneAlpha.Cur * notEx);
    for (int i = 0; i < 4; i++)
        if (dt->pNoStr[i]) DF_MOBJ_SetStringColor(dt->pNoStr[i], &col_a);
    struct _stMModel *m;
    if ((m = dt->pShadow) != 0) {
        m->mulalpha = sceneA;
        DF_MMODEL_Play(m);
    }
    if ((m = dt->pSpot) != 0) {
        m->mulalpha = ((float)dt->SelAlpha.Cur / 64.0f) * sceneA;
        DF_MMODEL_Play(m);
    }
    if ((m = dt->pNoCard) != 0) {
        m->mulalpha = (0.5f * notEx) * sceneA;
        DF_MMODEL_Play(m);
    }
    if ((m = dt->pCard) != 0) {
        unsigned char v = (unsigned char)(dt->SelAlpha.Cur + 0x40);   /* no clamp */
        col_b.a = 0x80;
        col_b.r = v;
        col_b.g = v;
        col_b.b = v;
        DF_MMODEL_SetVertColor(m, 0, &col_b);
        m->mulalpha = existA * sceneA;
        DF_MMODEL_Play(m);
    }
}

/* pkt_func: VU1 packets of the four models (shadow, spot, no-card, card) */
static void __MC_MODEL_PktMdl(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    McModel *dt = msys_mc_model(ioObjp);
    if (!dt->bDraw) return;
    unsigned int *p = DF_MPKT_MakeVIFCode(MPktCurrentp, 0, 0, 0, 0x13000000);
    if (dt->pShadow) p = DF_MMODEL_MakeVu1PacketAC(p, dt->pShadow, 0, 2);
    if (dt->pSpot) p = DF_MMODEL_MakeVu1PacketAC(p, dt->pSpot, 0, 2);
    if (dt->pNoCard) p = DF_MMODEL_MakeVu1PacketAC(p, dt->pNoCard, 0, 2);
    if (dt->pCard) p = DF_MMODEL_MakeVu1PacketAC(p, dt->pCard, 0, 2);
    MPktCurrentp = p;
}

static void __MC_MODEL_Delete(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    if (!ioObjp) return;
    McModel *dt = msys_mc_model(ioObjp);
    dt->bDraw = 0;
    for (int i = 0; i < 2; i++)
        if (dt->pName[i]) { msys_erase(dt->pName[i]); dt->pName[i] = 0; }
    for (int i = 0; i < 4; i++)
        if (dt->pNoStr[i]) { msys_erase(dt->pNoStr[i]); dt->pNoStr[i] = 0; }
    if (dt->pShadow) DF_MMODEL_DeleteModelAndMMD(dt->pShadow);
    dt->pShadow = 0;
    if (dt->pSpot) DF_MMODEL_DeleteModelAndMMD(dt->pSpot);
    dt->pSpot = 0;
    if (dt->pNoCard) DF_MMODEL_DeleteModelAndMMD(dt->pNoCard);
    dt->pNoCard = 0;
    if (dt->pCard) DF_MMODEL_DeleteModelAndMMD(dt->pCard);
    dt->pCard = 0;
    msys_erase(ioObjp);
}

/* one model from the shell's model file (s_msysFileMng, read by msys_FileReadSync); NULL when the file is not loaded */
static struct _stMModel *msys_mc_model_new(int idx, float (*posp)[4], struct struct_76b810ec *colp, float mulalpha,
                                           unsigned long long alpha_reg)
{
    if (!(s_msysFileMng.pData && s_msysFileMng.bRead)) return 0;
    void *data = DF_MDATA_GetData(s_msysFileMng.pData, idx);   /* result not tested */
    struct _stMModel *m = DF_MGEN_NewModelEasyFromMMD(0, data, posp, 0, 0, colp);
    if (m) {
        m->test_reg = 0x50000;
        m->mulalpha = mulalpha;
        m->alpha_reg = alpha_reg;
    }
    return m;
}

static struct stMobj *__MC_MODEL_New(struct stMobj *inObjp, int inSlot)
{
    PS2_FRAME(0x140);
    PS2_STACK(float[4][4], pos, 0xa0);                    /* to interpreted DF_MGEN_NewModelEasyFromMMD */
    PS2_STACK(int[4], strno, 0xe0);
    PS2_STACK(struct struct_214acd28[4], locno, 0xf0);
    PS2_STACK(struct struct_214acd28[2], locmc, 0x110);
    PS2_STACK(struct struct_76b810ec[2], col2, 0x128);    /* to interpreted DF_MGEN_NewModelEasyFromMMD */
    PS2_STACK(int[2], strmc, 0x130);
    PS2_STACK(struct struct_76b810ec[2], col, 0x138);
    struct stMobj *obj = DF_MOBJ_GetNewObject(inObjp);
    if (!obj) return 0;
    McModel *dt = msys_mc_model(obj);
    obj->type = 0;
    obj->stat = 0;
    obj->count = 0;
    obj->play_func = (void (*)(void))GFN(__MC_MODEL_Update);
    obj->pkt_func = (void (*)(void))GFN(__MC_MODEL_PktMdl);
    dt->slotNum = inSlot;
    msys_colour(&col[0], 0x808c826e);   /* sdata 0x27bdc8 */
    msys_colour(&col[1], 0x408c826e);
    static const int k_locmc[2][2] = {{0x60, 0x82}, {0x92, 0x92}};                            /* OLMENU 0x1ee01a0 */
    static const int k_locno[4][2] = {{0xc4, 0xd2}, {0xa8, 0xde}, {0xb6, 0xea}, {0xb7, 0xf6}};  /* 0x1ee01b0 */
    static const int k_strno[4] = {0x192dd, 0x192de, 0x192df, 0x192e0};                        /* 0x1ee01d0 */
    for (int i = 0; i < 2; i++) { locmc[i].x = k_locmc[i][0]; locmc[i].y = k_locmc[i][1]; }
    for (int i = 0; i < 4; i++) { locno[i].x = k_locno[i][0]; locno[i].y = k_locno[i][1]; }
    strmc[0] = 0x1928e;   /* sdata 0x27bdd0 */
    strmc[1] = 0x1928f;
    for (int i = 0; i < 4; i++) strno[i] = k_strno[i];
    if (inSlot != 0) {
        strmc[1] = 0x19290;
        locmc[0].x += 0xf0;
        locmc[1].x += 0xf0;
        for (int i = 0; i < 4; i++) locno[i].x += 0xe8;
    }
    for (int i = 0; i < 2; i++)
        if (!dt->pName[i])
            dt->pName[i] = DF_MOBJ_NewString(obj, 0, strmc[i], 0, 4, locmc[i].x, locmc[i].y, &col[1], 0, 0);
    for (int i = 0; i < 4; i++)
        if (!dt->pNoStr[i])
            dt->pNoStr[i] = DF_MOBJ_NewString(obj, 0, strno[i], 0, 1, locno[i].x, locno[i].y, &col[0], 0, 0);
    /* OLMENU 0x1ee01e0: card row y 0, shadow / spot row y -0.1 */
    static const unsigned int k_pos[4][4] = {
        {0xc059999a, 0, 0, 0x3f800000}, {0x4059999a, 0, 0, 0x3f800000},
        {0xc059999a, 0xbdcccccd, 0, 0x3f800000}, {0x4059999a, 0xbdcccccd, 0, 0x3f800000}};
    memcpy(pos, k_pos, sizeof(k_pos));
    msys_colour(&col2[0], 0x80808080);   /* sdata 0x27bdd8 */
    msys_colour(&col2[1], 0x80404040);
    if (!dt->pShadow) dt->pShadow = msys_mc_model_new(3, &pos[inSlot + 2], &col2[0], 1.0f, 0x42);
    if (!dt->pSpot) dt->pSpot = msys_mc_model_new(7, &pos[inSlot + 2], &col2[0], 0.0f, 0x48);
    if (!dt->pNoCard) dt->pNoCard = msys_mc_model_new(2, &pos[inSlot], &col2[0], 0.5f, 0x44);
    if (!dt->pCard) dt->pCard = msys_mc_model_new(inSlot, &pos[inSlot], &col2[1], 1.0f, 0x44);
    MoverI_Init(&dt->ExistAlpha);
    MoverI_Init(&dt->SelAlpha);
    MoverI_Init(&dt->SceneAlpha);
    dt->bExist = 0;
    MoverI_SetValue(&dt->ExistAlpha, 0x80);
    MoverI_Move(&dt->ExistAlpha, 0, 5);
    dt->bDraw = 1;
    return obj;
}

/* ---- MC shell: memory-card engine start / stop (struct_5ef11617 in dt) --------------------------------------------- */

/* play_func of the poll object: run the card engine, refresh the 2 x 8 file list infos every frame */
static void __PlayMcProc(void)
{
    PS2_FRAME(0x70);
    Mcf_Proc_Ret = Ac2_Mcf_Proc(1);
    if (Mcf_Proc_Ret == -1) return;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 8; j++) mc_ret[i][j] = Ac2_Mcf_GetListInfo(&mc_info[i][j], i + 1, j);
}

static void __StopMcSystemMcf(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    McShell *dt = msys_mc_shell(ioObjp);
    if (dt->mcProc) msys_erase(dt->mcProc);
    dt->mcProc = 0;
    Ac2_Mcf_Stop();
    while (Ac2_Mcf_Proc(0) != 1) {}   /* original busy-wait: a -1 return never ends it */
    if (Mcf_Start_Buf) {
        DF_MMM_Free(0, Mcf_Start_Buf);
        Mcf_Start_Buf = 0;
    }
    Ac2_Mcf_SetWorkBuffer(0, 0);
    if (dt->pWork) DF_MMM_Free(0, dt->pWork);
    dt->pWork = 0;   /* Mcf_Start_Info is left dangling */
}

static void __StartMcSystemMcf(struct stMobj *ioObjp, int inSelect, int inLoadAc)
{
    PS2_FRAME(0x50);
    McShell *dt = msys_mc_shell(ioObjp);
    Ac2_Mcf_Init();
    Ac2_Mcf_SetTitle(inSelect == 7 ? 4 : 5);
    int type, mode;
    switch (inSelect) {   /* jump table OLMENU 0x1ee6110 */
    case 0:
    case 1: type = 0; mode = 5; break;
    case 2: type = 1; mode = 5; break;
    case 3:
        type = 1;
        if (inLoadAc == 0) mode = 5;
        else if (inLoadAc == type) mode = 4;
        else mode = 3;
        break;
    case 6: type = 0; mode = 5; break;
    case 7: type = 0; mode = 4; break;
    default: type = 0; mode = 5; break;   /* 4, 5 and out of range */
    }
    dt->pWork = DF_MMM_Alloc(0, 0x2000);
    if (dt->pWork) Ac2_Mcf_SetWorkBuffer(dt->pWork, 8);
    Mcf_Start_Buf = DF_MMM_Alloc(0, 0x100000);   /* not NULL-tested */
    /* the store of Start's result sits in GetMcInfo's jal delay slot: Mcf_Start_Info = Start's return value,
     * GetMcInfo's (always 0) is dropped */
    Mcf_Start_Info = Ac2_Mcf_Start(&dt->mcInfo[0], &dt->mcInfo[1], type, Mcf_Start_Buf);
    Ac2_Mcf_GetMcInfo();
    Mcf_Start_Info = DF_MFUNC_Aligned(Mcf_Start_Info, 0x40);
    ChangeConvertMode(mode);
    if (!dt->mcProc) {
        struct stMobj *o = DF_MOBJ_GetNewObject(ioObjp);
        if (o) {
            o->type = 0;
            o->stat = 0;
            o->play_func = (void (*)(void))GFN(__PlayMcProc);
            unsigned int *w = (unsigned int *)&o->dt;   /* never read by __PlayMcProc */
            w[0] = PS2PTR(&dt->mcInfo[0]);
            w[1] = PS2PTR(&dt->mcInfo[1]);
        }
        dt->mcProc = o;
    }
}

/* ---- MC shell: card A / B selection, dialogs, dispatch to the Game / Emblem lists, constructors (group 6) ---------- */

/* inlined "leave the menu" exit: fade the whole menu out, the shell dies in __PlayMcSystemExitFadeOut */
static void msys_shell_exit_fade(struct stMobj *ioObjp)
{
    DF_MENU_SetFadeOut(0x1e);
    DF_MBACK_SetFadeOutNX();   /* the asm passes a0 = 0; DWARF has no parameter */
    DF_MBGUIDE_Hide();
    DF_MBACK_ClearSceneLabel();
    msys_mc_shell(ioObjp)->step = 0;
    ioObjp->count = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = (void (*)(void))GFN(__PlayMcSystemExitFadeOut);
}

/* inlined end of the shell: scene label / key guide, stop the card engine, release the model file (shell mode only) */
static void msys_shell_end(struct stMobj *ioObjp)
{
    if (gMcMode == 1) DF_MBACK_PopSceneLabel();
    else DF_MBGUIDE_Hide();
    __StopMcSystemMcf(ioObjp);
    if (s_msysFileMng.Start == 0x1e) {
        if (s_msysFileMng.pData != 0 && s_msysFileMng.bRead != 0) DF_MMM_Free(0, s_msysFileMng.pData);
        s_msysFileMng.bRead = 0;
        s_msysFileMng.pData = 0;
        s_msysFileMng.Step = 0;
        IconInfo.pGame = 0;
        IconInfo.pEmb = 0;
        IconInfo.pCom = 0;
    }
    ioObjp->stat = 5;
    ioObjp->play_func = 0;
}

/* inlined: fade both card models out, then __PlayMcSystemFadeOut */
static void msys_shell_scene_fade_out(struct stMobj *ioObjp)
{
    McShell *dt = msys_mc_shell(ioObjp);
    for (int k = 0; k < 2; k++) {
        struct stMobj *m = dt->mcMdl[k];
        if (m) {
            MoverI_SetValue(&msys_mc_model(m)->SceneAlpha, 0x80);
            MoverI_Move(&msys_mc_model(m)->SceneAlpha, 0, 10);
        }
    }
    DF_M1HELP_FadeOut();
    dt->step = 0;
    ioObjp->count = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = (void (*)(void))GFN(__PlayMcSystemFadeOut);
}

/* inlined: highlight the selected card model, dim the other one (mcIdx not range-checked) */
static void msys_shell_select_models(McShell *dt)
{
    struct stMobj *m = dt->mcMdl[(int)dt->mcIdx];
    if (m) {
        msys_mc_model(m)->bSelect = 1;
        MoverI_SetValue(&msys_mc_model(m)->SelAlpha, 0);
        MoverI_Move(&msys_mc_model(m)->SelAlpha, 0x40, 5);
    }
    m = dt->mcMdl[dt->mcIdx ^ 1];
    if (m) {
        msys_mc_model(m)->bSelect = 0;
        MoverI_SetValue(&msys_mc_model(m)->SelAlpha, 0x40);
        MoverI_Move(&msys_mc_model(m)->SelAlpha, 0, 5);
    }
}

/* "card not formatted: format it?" -> format -> result dialogs; card pulled -> __DlgMcSystemAlart */
static void __DlgMcSystemFormat(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    PS2_STACK(int, res, 0x3c);
    McShell *dt = msys_mc_shell(ioObjp);
    switch (dt->step) {
    case 0x63:
        if (__SyncDialog(0) < 4) return;
        dt->dlgMsgId = 0x30e0b;
        dt->step = 0;
        ioObjp->play_func = (void (*)(void))GFN(__DlgMcSystemAlart);
        break;
    case 0x15:
        if (__SyncDialog(0) < 4) return;
        dt->step = 0;
        ioObjp->stat = 0;
        ioObjp->play_func = (void (*)(void))GFN(__PlayMcSystem);
        break;
    case 0x14:
        if (__SyncDialog(0) < 4) return;
        msys_dlg(5, 0x30e0f, 0, 0);
        dt->step++;
        break;
    case 0xb:
        if (__SyncDialog(0) < 4) return;
        dt->step = 0;
        ioObjp->play_func = (void (*)(void))GFN(__PlayMcSystemNext);
        break;
    case 0xa:
        if (__SyncDialog(0) < 4) return;
        msys_dlg(5, 0x30e0e, 0, 0);
        dt->step++;
        break;
    case 3:
        if (Mcf_Proc_Ret > 0) {
            DF_MDIALOG_Close();
            Ac2_Mcf_GetMcInfo();
            dt->step = 0xa;
        } else if (Mcf_Proc_Ret < 0) {
            DF_MDIALOG_Close();
            Ac2_Mcf_GetMcInfo();
            dt->step = 0x14;
        }
        break;
    case 2:
        msys_dlg(5, 0x30e0d, 0, 1);
        dt->step++;
        break;
    case 1:
        if (GetPortStatus(dt->mcIdx) == 0 || Mcf_Proc_Ret == -1) {
            DF_MDIALOG_Close();
            dt->step = 0x63;
            return;
        }
        if (__SyncDialog(&res) < 4) return;
        if (res == 1) {
            Ac2_Mcf_Format(dt->mcIdx + 1);
            dt->step++;
        } else {
            dt->step = 0;
            ioObjp->stat = 0;
            ioObjp->play_func = (void (*)(void))GFN(__PlayMcSystem);
        }
        break;
    case 0:
        msys_dlg(2, 0x30e0c, 1, 0);
        dt->step++;
        break;
    }
}

/* Cancel with f_clear_data: "leave without saving?" yes -> fade the shell out, no -> back to the card selection */
static void __DlgMcSystemExitSave(struct stMobj *ioObjp)
{
    PS2_FRAME(0x50);
    PS2_STACK(int, res, 0x4c);
    McShell *dt = msys_mc_shell(ioObjp);
    if (dt->step == 1) {
        if (__SyncDialog(&res) < 4) return;
        if (res == 1) {
            msys_shell_scene_fade_out(ioObjp);
        } else {
            dt->step = 0;
            ioObjp->stat = 0;
            ioObjp->play_func = (void (*)(void))GFN(__PlayMcSystem);
        }
    } else if (dt->step == 0) {
        msys_dlg(2, dt->dlgMsgId, 1, 0);
        dt->step++;
    }
}

/* one-button message dialog (dlgMsgId), then back to the card selection */
static void __DlgMcSystemAlart(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    McShell *dt = msys_mc_shell(ioObjp);
    if (dt->step == 1) {
        if (__SyncDialog(0) < 4) return;
        dt->step = 0;
        ioObjp->stat = 0;
        ioObjp->play_func = (void (*)(void))GFN(__PlayMcSystem);
    } else if (dt->step == 0) {
        msys_dlg(5, dt->dlgMsgId, 0, 0);
        dt->step++;
    }
}

/* the Game / Emblem list object ended: leave the menu, end the shell or go back to the card selection (callDir 1) */
static void __PlayMcSystemDispatch(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    McShell *dt = msys_mc_shell(ioObjp);
    struct stMobj *cs = dt->csysp;
    /* original reads csysp->stat without a NULL test (PS2 address 2) */
    unsigned char st = cs ? cs->stat : ((const unsigned char *)PS2_NULL_MEM())[2];
    if (st != 5) return;
    if (cs) {
        msys_erase(cs);
        dt->csysp = 0;
    }
    if (MenuMng.stat == 4) {
        if (gMcMode != 1 && dt->bFadeOut != 0) msys_shell_exit_fade(ioObjp);
        else msys_shell_end(ioObjp);
    } else if (dt->f_clear_data == 1 && s_msysMcResult == 1) {
        if (dt->bFadeOut != 0) msys_shell_exit_fade(ioObjp);
        else msys_shell_end(ioObjp);
    } else if (gMcMode != 1 && dt->select == 0 && s_msysMcResult == 1) {
        if (dt->bFadeOut != 0) msys_shell_exit_fade(ioObjp);
        else msys_shell_end(ioObjp);
    } else {
        ioObjp->stat = 1;
        ioObjp->play_func = (void (*)(void))GFN(__PlayMcSystemFadeIn);
        ioObjp->count = 0;
        dt->step = 0;
        dt->callDir = 1;
    }
}

/* wait for the menu fade (30 frames), then end the shell */
static void __PlayMcSystemExitFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    McShell *dt = msys_mc_shell(ioObjp);
    if (dt->step == 1) {
        msys_shell_end(ioObjp);
    } else if (dt->step == 0) {
        ioObjp->count++;
        if (ioObjp->count < 0x1e) return;
        dt->step++;
    }
}

/* shell fade-out: delete the card models after 5 frames, then dispatch (a list object is running) or end */
static void __PlayMcSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    McShell *dt = msys_mc_shell(ioObjp);
    if (dt->step == 1) {
        if (dt->csysp != 0) {
            ioObjp->stat = 3;
            ioObjp->play_func = (void (*)(void))GFN(__PlayMcSystemDispatch);
            return;
        }
        if (gMcMode != 1 && MenuMng.stat != 4 && dt->select == 3) DF_MBACK_PopSceneLabel();
        if (dt->bFadeOut != 0) msys_shell_exit_fade(ioObjp);
        else msys_shell_end(ioObjp);
    } else if (dt->step == 0) {
        ioObjp->count++;
        if (ioObjp->count < 5) return;
        for (int k = 0; k < 2; k++) {
            if (dt->mcMdl[k]) __MC_MODEL_Delete(dt->mcMdl[k]);
            dt->mcMdl[k] = 0;
        }
        dt->step++;
    }
}

/* Decide on a card: wait for the card info, check card / format / free space, start the Game or Emblem list */
static void __PlayMcSystemNext(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    PS2_STACK(int[2], msg, 0x58);
    McShell *dt = msys_mc_shell(ioObjp);
    if (dt->step == 1) {
        int idx = dt->mcIdx;
        if (!dt->mcInfo[idx].f_exist) {
            msg[0] = 0x30e08;   /* sdata 0x27bdc0 */
            msg[1] = 0x30e09;
            dt->dlgMsgId = msg[idx];
            dt->step = 0;
            ioObjp->play_func = (void (*)(void))GFN(__DlgMcSystemAlart);
            return;
        }
        if (!__ChkMcSystemNext(ioObjp)) return;
        switch (dt->select) {   /* jump table OLMENU 0x1ee6130 */
        case 0:
        case 1:
        case 6:
        case 7:
            dt->csysp = __NewMcGameSystem(ioObjp, dt->player, dt->select, dt->loadAc, dt->mcIdx, 0,
                                          &dt->mcInfo[(int)dt->mcIdx], dt->argp, dt->argp2);
            break;
        case 2:
        case 3:
            dt->csysp = __NewMcEmbSystem(ioObjp, dt->player, dt->select, dt->loadAc, dt->mcIdx, 0,
                                         &dt->mcInfo[(int)dt->mcIdx], 0, 0);
            break;
        default:   /* 4, 5, out of range: no list */
            break;
        }
        msys_shell_scene_fade_out(ioObjp);
    } else if (dt->step == 0) {
        if (Ac2_Mcf_CheckGetInfoStep() != 0) dt->step++;
    }
}

/* 1 = go on; 0 = a dialog play_func was installed (unformatted card, too little space, no file to load) */
static int __ChkMcSystemNext(struct stMobj *ioObjp)
{
    McShell *dt = msys_mc_shell(ioObjp);
    int sel = dt->select;
    if (sel == 0 || sel == 2 || sel == 6) {
        struct struct_87202900 *e = &dt->mcInfo[(int)dt->mcIdx];
        if (!e->f_fmt) {
            dt->step = 0;
            ioObjp->play_func = (void (*)(void))GFN(__DlgMcSystemFormat);
            return 0;
        }
        if (e->snum != 0) return 1;
        int msg, need;
        if (sel == 2) {
            msg = 0x30e1c;
            need = 0x3c;
        } else {
            msg = 0x30e15;
            need = 0xa0;
        }
        if (e->free_size < need) {
            dt->dlgMsgId = msg;
            dt->step = 0;
            ioObjp->play_func = (void (*)(void))GFN(__DlgMcSystemAlart);
            return 0;
        }
        return 1;
    }
    if (dt->mcInfo[(int)dt->mcIdx].snum != 0) return 1;
    if (sel == 1) dt->dlgMsgId = 0x30e34;
    else if (sel == 7) dt->dlgMsgId = 0x30e30;
    else dt->dlgMsgId = dt->loadAc == 0 ? 0x30e35 : 0x30e31;
    dt->step = 0;
    ioObjp->play_func = (void (*)(void))GFN(__DlgMcSystemAlart);
    return 0;
}

/* idle: card A / B selection (key guide, cancel, decide, left / right) */
static void __PlayMcSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    McShell *dt = msys_mc_shell(ioObjp);
    int guide = dt->select == 3 ? 0x36 : (dt->bFadeOut != 0 ? 0x2f : 7);
    if (MenuKeyGuide_IsShown()) return;
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    if (pad->start == 1) {
        MenuKeyGuide_Show(guide);
        return;
    }
    if (pad->Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        if (dt->f_clear_data == 1) {
            dt->dlgMsgId = 0x30e4e;
            dt->step = 0;
            ioObjp->play_func = (void (*)(void))GFN(__DlgMcSystemExitSave);
            return;
        }
        msys_shell_scene_fade_out(ioObjp);
        return;
    }
    if (pad->Rdw == 1) {
        Voice_Call(0x3ea, 0x7f);
        dt->step = 0;
        ioObjp->play_func = (void (*)(void))GFN(__PlayMcSystemNext);
        return;
    }
    if (pad->repeat.lt == 1 || pad->repeat.rt == 1) {
        Voice_Call(0x3e9, 0x7f);
        dt->mcIdx ^= 1;
        msys_shell_select_models(dt);
    }
}

/* shell fade-in (switch with fall-through, jump table OLMENU 0x1ee6150): help, "reading" dialog, model file, card
 * models, camera motion */
static void __PlayMcSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    McShell *dt = msys_mc_shell(ioObjp);
    switch (dt->step) {
    case 0:
        ioObjp->count++;
        if (ioObjp->count < 5) return;
        DF_MVIEW_MotionStart(MViewp, 0xac);
        DF_M1HELP_SetStringIdCopy(0x192da);
        DF_M1HELP_FadeIn();
        DF_MBGUIDE_Show();
        if (dt->callDir == 0) msys_dlg(5, 0x30e0a, 0, 1);
        dt->step++;
        /* fall through */
    case 1:
        if (msys_FileReadSync() != 0) return;
        dt->step++;
        /* fall through */
    case 2:
        if (dt->callDir == 0) {
            if (dt->mcInfo[0].stat == 1) return;
            if (dt->mcInfo[1].stat == 1) return;
            if (__SyncDialog(0) != 2) return;
            DF_MDIALOG_Close();
            dt->step++;
            return;
        }
        dt->step++;
        /* fall through */
    case 3:
        if (dt->callDir == 0) {
            /* original: advances while the dialog is NOT finished (slti 4 / beqz) */
            if (__SyncDialog(0) >= 4) return;
            dt->step++;
            return;
        }
        dt->step++;
        /* fall through */
    case 4:
        if (!dt->mcMdl[0]) dt->mcMdl[0] = __MC_MODEL_New(ioObjp, 0);
        if (!dt->mcMdl[1]) dt->mcMdl[1] = __MC_MODEL_New(ioObjp, 1);
        msys_shell_select_models(dt);
        for (int k = 0; k < 2; k++) {
            struct stMobj *m = dt->mcMdl[k];
            if (m) {
                MoverI_SetValue(&msys_mc_model(m)->SceneAlpha, 0);
                MoverI_Move(&msys_mc_model(m)->SceneAlpha, 0x80, 10);
            }
        }
        dt->step++;
        break;
    case 5:
        if (DF_MVIEW_MotionSync(MViewp) != 0) {
            ioObjp->stat = 0;
            ioObjp->play_func = (void (*)(void))GFN(__PlayMcSystem);
        }
        break;
    }
}

/* the MC shell object: starts the card engine and pushes the scene label (select 0..6 -> 0x47 + select, else 0x4e) */
static struct stMobj *__NewMcSystem(struct stMobj *inObjp, int inPlayer, int inSelect, int inLoadAc, void *argp,
                                    void *argp2)
{
    PS2_FRAME(0x70);
    struct stMobj *o = DF_MOBJ_GetNewObject(inObjp);
    if (!o) return 0;
    McShell *dt = msys_mc_shell(o);
    o->type = 0;
    o->stat = 1;
    o->play_func = (void (*)(void))GFN(__PlayMcSystemFadeIn);
    o->count = 0;
    dt->select = inSelect;
    dt->player = inPlayer;
    dt->loadAc = inLoadAc;
    dt->step = 0;
    dt->callDir = 0;
    dt->mcIdx = 0;
    dt->argp = argp;
    dt->argp2 = argp2;
    int sel = dt->select;
    if ((unsigned int)sel < 2 || sel == 6 || sel == 7)
        dt->mcIdx = gMcMode == 1 ? 0 : (signed char)MenuMng.mc_slot;
    __StartMcSystemMcf(o, inSelect, inLoadAc);
    if (inSelect < 7) DF_MBACK_PushSceneLabel((enum_5124d09c)((inSelect + 0x47) & 0xff));
    else DF_MBACK_PushSceneLabel((enum_5124d09c)0x4e);
    s_msysMcResult = 0;
    return o;
}

/* save flow entry (DF_MMANAGE_NewSystem): shell mode file manager, f_clear_data for select 0, leave via menu fade */
struct stMobj *DF_MSYS_NewMcSystemForSave(struct stMobj *inObjp, int inPlayer, int inSelect, int inLoadAc, void *argp,
                                          void *argp2)
{
    PS2_FRAME(0x20);
    s_msysFileMng.bRead = 0;
    s_msysFileMng.pData = 0;
    s_msysFileMng.Step = 0;
    s_msysFileMng.Start = 0x1e;
    IconInfo.pGame = 0;
    IconInfo.pEmb = 0;
    IconInfo.pCom = 0;
    struct stMobj *o = __NewMcSystem(inObjp, inPlayer, inSelect, inLoadAc, argp, argp2);
    if (o) {
        McShell *dt = msys_mc_shell(o);
        dt->f_clear_data = inSelect == 0 ? 1 : 0;
        dt->bFadeOut = 1;
    }
    return o;
}

/* entry (DF_MMANAGE_NewSystem): select 3 = title emblem load (group 7 object, title mode file manager), else the shell */
struct stMobj *DF_MSYS_NewMcSystem(struct stMobj *inObjp, int inPlayer, int inSelect, int inLoadAc, void *argp,
                                   void *argp2)
{
    PS2_FRAME(0x30);
    if (inSelect == 3) {
        s_msysFileMng.bRead = 0;
        s_msysFileMng.Start = 0x14;
        s_msysFileMng.pData = 0;
        s_msysFileMng.Step = 0;
        IconInfo.pGame = 0;
        IconInfo.pEmb = 0;
        IconInfo.pCom = 0;
        struct stMobj *o = DF_MOBJ_GetNewObject(inObjp);
        if (!o) return 0;
        o->type = 0;
        o->stat = 1;
        o->play_func = (void (*)(void))GFN(__PlayEmblemLoadSystemFadeIn);
        o->pkt_func = (void (*)(void))GFN(__PlayEmblemLoadSystemPkt);
        o->count = 0;
        *(char *)&o->dt = (char)inPlayer;   /* TITLESELECT-like dt (group 7): player at +0 */
        DF_MBACK_PushSceneLabel((enum_5124d09c)0x4a);
        return o;
    }
    s_msysFileMng.bRead = 0;
    s_msysFileMng.pData = 0;
    s_msysFileMng.Start = 0x1e;
    s_msysFileMng.Step = 0;
    IconInfo.pGame = 0;
    IconInfo.pEmb = 0;
    IconInfo.pCom = 0;
    struct stMobj *o = __NewMcSystem(inObjp, inPlayer, inSelect, inLoadAc, argp, argp2);
    if (o) msys_mc_shell(o)->bFadeOut = 1;
    return o;
}

/* ---- title "emblem load" selector (TITLESELECT_*, __PlayEmblemLoadSystem*), System-menu refusal dialog, model file
 *      loader, DF_InitMemCard (group 7) ---------------------------------------------------------------------------- */

typedef struct struct_20bdd4d2 TitleSel;   /* title emblem-load selector (dt) */
typedef struct struct_0a180632 TitleEnt;   /* one of its two titles */

static inline TitleSel *msys_title(struct stMobj *o) { return (TitleSel *)&o->dt; }

/* pkt_func part 1: movers, label colours and model alphas of both titles */
static void __TITLESELECT_Update(struct stMobj *ioObjp)
{
    PS2_FRAME(0xb0);
    PS2_STACK(struct struct_76b810ec, col_b, 0xa8);   /* to interpreted DF_MMODEL_SetVertColor */
    PS2_STACK(struct struct_76b810ec, col_a, 0xac);
    TitleSel *dt = msys_title(ioObjp);
    if (!dt->bDraw) return;
    MoverI_Update(&dt->Title[0].alpha);
    MoverI_Update(&dt->Title[1].alpha);
    MoverI_Update(&dt->SceneAlpha);
    float sceneA = (float)dt->SceneAlpha.Cur / 128.0f;
    for (int k = 0; k < 2; k++) {
        TitleEnt *T = &dt->Title[k];
        msys_colour(&col_a, 0x408c826e);   /* sdata 0x27bdb8, reloaded per title */
        col_a.a = (unsigned char)msys_ftou((float)(T->alpha.Cur + 0x40) * sceneA);
        if (T->name[0]) DF_MOBJ_SetStringColor(T->name[0], &col_a);
        if (T->name[1]) DF_MOBJ_SetStringColor(T->name[1], &col_a);
        struct _stMModel *m;
        if ((m = T->shadow) != 0) {
            m->mulalpha = sceneA;
            DF_MMODEL_Play(m);
        }
        if ((m = T->spot) != 0) {
            m->mulalpha = ((float)T->alpha.Cur / 64.0f) * sceneA;   /* no +0x40 here */
            DF_MMODEL_Play(m);
        }
        if ((m = T->mdl) != 0) {
            unsigned char v = (unsigned char)(T->alpha.Cur + 0x40);   /* no clamp */
            col_b.r = v;
            col_b.g = v;
            col_b.b = v;
            col_b.a = 0x80;
            DF_MMODEL_SetVertColor(m, 0, &col_b);
            m->mulalpha = sceneA;
            DF_MMODEL_Play(m);
        }
    }
}

/* pkt_func part 2: VU1 packets of the six models */
static void __TITLESELECT_Pkt(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    TitleSel *dt = msys_title(ioObjp);
    if (!dt->bDraw) return;
    unsigned int *p = DF_MPKT_MakeVIFCode(MPktCurrentp, 0, 0, 0, 0x13000000);
    for (int k = 0; k < 2; k++) {
        TitleEnt *T = &dt->Title[k];
        if (T->shadow) p = DF_MMODEL_MakeVu1PacketAC(p, T->shadow, 0, 2);
        if (T->spot) p = DF_MMODEL_MakeVu1PacketAC(p, T->spot, 0, 2);
        if (T->mdl) p = DF_MMODEL_MakeVu1PacketAC(p, T->mdl, 0, 2);
    }
    MPktCurrentp = p;
}

/* strings and models of both titles (the object itself and its movers stay) */
static void __TITLESELECT_Delete(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    TitleSel *dt = msys_title(ioObjp);
    dt->bDraw = 0;
    for (int k = 0; k < 2; k++)
        for (int j = 0; j < 2; j++)
            if (dt->Title[k].name[j]) {
                msys_erase(dt->Title[k].name[j]);
                dt->Title[k].name[j] = 0;
            }
    for (int k = 0; k < 2; k++) {
        TitleEnt *T = &dt->Title[k];
        if (T->shadow) DF_MMODEL_DeleteModelAndMMD(T->shadow);
        T->shadow = 0;
        if (T->spot) DF_MMODEL_DeleteModelAndMMD(T->spot);
        T->spot = 0;
        if (T->mdl) DF_MMODEL_DeleteModelAndMMD(T->mdl);
        T->mdl = 0;
    }
}

/* one model of the model file with a scale vector (NULL when the file is not loaded) */
/* the asm re-tests bRead before DF_MDATA_GetData (NULL data if it changed): nothing runs in between, not mirrored */
static struct _stMModel *msys_title_model_new(int idx, float (*posp)[4], float (*sclp)[4], struct struct_76b810ec *colp,
                                              float mulalpha, unsigned long long alpha_reg)
{
    if (!(s_msysFileMng.pData && s_msysFileMng.bRead)) return 0;
    void *data = DF_MDATA_GetData(s_msysFileMng.pData, idx);   /* result not tested */
    struct _stMModel *m = DF_MGEN_NewModelEasyFromMMD(0, data, posp, 0, sclp, colp);
    if (m) {
        m->test_reg = 0x50000;
        m->mulalpha = mulalpha;
        m->alpha_reg = alpha_reg;
    }
    return m;
}

/* the two title labels (selected one brighter) and the shadow / spot / title models; no "slot empty" guard on models */
static void __TITLESELECT_New(struct stMobj *ioObjp)
{
    PS2_FRAME(0x140);
    PS2_STACK(unsigned long long[2], outarg, 0);
    PS2_STACK(float[2][4], scl, 0xa0);                    /* to interpreted DF_MGEN_NewModelEasyFromMMD */
    PS2_STACK(float[4][4], pos, 0xc0);
    PS2_STACK(int[4], strno, 0x100);
    PS2_STACK(struct struct_214acd28[4], rect, 0x110);
    PS2_STACK(struct struct_76b810ec[2], col2, 0x130);    /* to interpreted DF_MGEN_NewModelEasyFromMMD */
    PS2_STACK(struct struct_76b810ec[2], col, 0x138);
    TitleSel *dt = msys_title(ioObjp);
    msys_colour(&col[0], 0x408c826e);   /* sdata 0x27bda8 */
    msys_colour(&col[1], 0x808c826e);
    static const int k_rect[4][2] = {{0x4f, 0x68}, {0x54, 0x78}, {0x159, 0x68}, {0x19a, 0x78}};   /* OLMENU 0x1ee0110 */
    static const int k_strno[4] = {0x1928c, 0x1928d, 0x1928a, 0x1928b};                          /* 0x1ee0130 */
    for (int i = 0; i < 4; i++) { rect[i].x = k_rect[i][0]; rect[i].y = k_rect[i][1]; }
    for (int i = 0; i < 4; i++) strno[i] = k_strno[i];
    int n = 0;   /* runs across both titles */
    for (int k = 0; k < 2; k++)
        for (int j = 0; j < 2; j++, n++)
            if (!dt->Title[k].name[j]) {
                outarg[0] = 0;   /* args 9 / 10 on the PS2 stack, mirrored though the callee is native */
                outarg[1] = 0;
                dt->Title[k].name[j] = DF_MOBJ_NewString(ioObjp, 0, strno[n], 0, 4, rect[n].x, rect[n].y,
                                                         &col[(k == dt->idx) ? 1 : 0], 0, 0);
            }
    /* OLMENU 0x1ee0140: title row y 0, shadow / spot row y -0.1; 0x1ee0180: shadow / spot scales */
    static const unsigned int k_pos[4][4] = {
        {0xc0a00000, 0, 0, 0x3f800000}, {0x40a00000, 0, 0, 0x3f800000},
        {0xc0a00000, 0xbdcccccd, 0, 0x3f800000}, {0x40a00000, 0xbdcccccd, 0, 0x3f800000}};
    static const unsigned int k_scl[2][4] = {
        {0x3fc00000, 0x3f800000, 0x3fc00000, 0x3f800000}, {0x400ccccd, 0x3f800000, 0x400ccccd, 0x3f800000}};
    memcpy(pos, k_pos, sizeof(k_pos));
    memcpy(scl, k_scl, sizeof(k_scl));
    msys_colour(&col2[0], 0x80808080);   /* sdata 0x27bdb0 */
    msys_colour(&col2[1], 0x80404040);
    dt->Title[0].shadow = msys_title_model_new(3, &pos[2], &scl[0], &col2[0], 1.0f, 0x42);
    dt->Title[1].shadow = msys_title_model_new(3, &pos[3], &scl[0], &col2[0], 1.0f, 0x42);
    dt->Title[0].spot = msys_title_model_new(7, &pos[2], &scl[1], &col2[0], 0.0f, 0x48);
    dt->Title[1].spot = msys_title_model_new(7, &pos[3], &scl[1], &col2[0], 0.0f, 0x48);
    dt->Title[0].mdl = msys_title_model_new(4, &pos[0], 0, &col2[1], 1.0f, 0x44);
    dt->Title[1].mdl = msys_title_model_new(5, &pos[1], 0, &col2[1], 1.0f, 0x44);
    MoverI_Init(&dt->Title[0].alpha);
    MoverI_Init(&dt->Title[1].alpha);
    MoverI_Init(&dt->SceneAlpha);
    dt->bDraw = 1;
}

/* the Emblem Load shell ended: rebuild the title screen */
static void __PlayEmblemLoadSystemDispatch(struct stMobj *ioObjp)
{
    TitleSel *dt = msys_title(ioObjp);
    struct stMobj *cs = dt->csysp;
    /* original reads csysp->stat without a NULL test (PS2 address 2) */
    unsigned char st = cs ? cs->stat : ((const unsigned char *)PS2_NULL_MEM())[2];
    if (st != 5) return;
    if (cs) {
        msys_erase(cs);
        dt->csysp = 0;
    }
    ioObjp->stat = 1;
    ioObjp->play_func = (void (*)(void))GFN(__PlayEmblemLoadSystemFadeIn);
    ioObjp->count = 0;
    dt->step = 0;
}

/* titles gone after 5 frames; then wait for the shell (Dispatch) or end, releasing the model file in title mode */
static void __PlayEmblemLoadSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    TitleSel *dt = msys_title(ioObjp);
    switch (dt->step) {
    case 0:
        if (++ioObjp->count < 5) return;
        __TITLESELECT_Delete(ioObjp);
        dt->step++;
        break;
    case 1:
        if (dt->csysp != 0) {
            ioObjp->stat = 3;
            ioObjp->play_func = (void (*)(void))GFN(__PlayEmblemLoadSystemDispatch);
            return;
        }
        DF_MBACK_PopSceneLabel();
        if (s_msysFileMng.Start == 0x14) {   /* title mode (the shell releases in mode 0x1e) */
            if (s_msysFileMng.pData != 0 && s_msysFileMng.bRead != 0) DF_MMM_Free(0, s_msysFileMng.pData);
            s_msysFileMng.bRead = 0;
            s_msysFileMng.pData = 0;
            s_msysFileMng.Step = 0;
            IconInfo.pGame = 0;
            IconInfo.pEmb = 0;
            IconInfo.pCom = 0;
        }
        ioObjp->stat = 5;
        ioObjp->play_func = 0;
        break;
    }
}

static inline void msys_title_fade_out(struct stMobj *ioObjp)
{
    TitleSel *dt = msys_title(ioObjp);
    MoverI_SetValue(&dt->SceneAlpha, 0x80);
    MoverI_Move(&dt->SceneAlpha, 0, 10);
    DF_M1HELP_FadeOut();
    dt->step = 0;
    ioObjp->count = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = (void (*)(void))GFN(__PlayEmblemLoadSystemFadeOut);
}

/* highlight Title[idx], dim the other one */
static inline void msys_title_select(TitleSel *dt, int i)
{
    MoverI_SetValue(&dt->Title[i].alpha, 0);
    MoverI_Move(&dt->Title[i].alpha, 0x40, 5);
    int o = i ^ 1;
    MoverI_SetValue(&dt->Title[o].alpha, 0x40);
    MoverI_Move(&dt->Title[o].alpha, 0, 5);
}

/* idle: key guide, cancel, decide (opens the Emblem Load shell for the selected title), left / right */
static void __PlayEmblemLoadSystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    TitleSel *dt = msys_title(ioObjp);
    if (MenuKeyGuide_IsShown()) return;
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    if (pad->start == 1) {
        MenuKeyGuide_Show(0x35);
        return;
    }
    if (pad->Rrt == 1) {
        Voice_Call(0x3eb, 0x7f);
        msys_title_fade_out(ioObjp);
        return;
    }
    if (pad->Rdw == 1) {
        Voice_Call(0x3ea, 0x7f);
        dt->csysp = __NewMcSystem(ioObjp, dt->player, 3, dt->idx, 0, 0);
        msys_title_fade_out(ioObjp);
        return;
    }
    if (pad->repeat.lt == 1 || pad->repeat.rt == 1) {
        Voice_Call(0x3e9, 0x7f);
        dt->idx ^= 1;
        msys_title_select(dt, dt->idx);
    }
}

/* wait 5 frames, camera / help, load the model file, build the titles, wait for the camera */
static void __PlayEmblemLoadSystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    TitleSel *dt = msys_title(ioObjp);
    switch (dt->step) {
    case 0:
        if (++ioObjp->count < 5) return;
        DF_MVIEW_MotionStart(MViewp, 0xad);
        DF_M1HELP_SetStringId(0x192db);
        DF_M1HELP_FadeIn();
        DF_MBGUIDE_Show();
        dt->step++;
        /* fall through */
    case 1:
        if (msys_FileReadSync() != 0) return;
        dt->step++;
        /* fall through */
    case 2:
        __TITLESELECT_New(ioObjp);
        msys_title_select(dt, dt->idx);
        MoverI_SetValue(&dt->SceneAlpha, 0);
        MoverI_Move(&dt->SceneAlpha, 0x80, 10);
        dt->step++;
        break;
    case 3:
        if (DF_MVIEW_MotionSync(MViewp) != 0) {
            ioObjp->stat = 0;
            ioObjp->play_func = (void (*)(void))GFN(__PlayEmblemLoadSystem);
        }
        break;
    }
}

/* the title object's pkt_func */
static void __PlayEmblemLoadSystemPkt(struct stMobj *ioObjp)
{
    PS2_FRAME(0x20);
    __TITLESELECT_Update(ioObjp);
    __TITLESELECT_Pkt(ioObjp);
}

/* System menu: "only previously loaded data can be saved" dialog, then back to __PlaySystem */
static void __DlgSystemDisableLoad(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    struct struct_4550d8c9 *dt = (struct struct_4550d8c9 *)&ioObjp->dt;
    switch (dt->step) {
    case 0:
        msys_dlg(5, 0x30e40, 0, 0);
        dt->step++;
        break;
    case 1:
        if (__SyncDialog(0) < 4) return;
        ioObjp->play_func = (void (*)(void))GFN(__PlaySystem);
        break;
    }
}

/* reads the menu model file (0xfae) once: 0 = ready, 1 = working (no error check; Step > 1 stays 1 forever) */
static int msys_FileReadSync(void)
{
    PS2_FRAME(0x10);
    if (s_msysFileMng.pData != 0 && s_msysFileMng.bRead != 0) return 0;
    switch (s_msysFileMng.Step) {
    case 0:
        if (File_IsAsyncBusy()) return 1;
        s_msysFileMng.pData = DF_MMM_Alloc(0, File_GetSize(0xfae));
        File_Read(0xfae, -1, (char *)s_msysFileMng.pData, 0);
        s_msysFileMng.Step++;
        return 1;
    case 1:
        if (File_IsAsyncBusy()) return 1;
        s_msysFileMng.bRead = 1;
        IconInfo.pGame = (s_msysFileMng.pData != 0 && s_msysFileMng.bRead != 0)
                             ? (unsigned char *)DF_MDATA_GetData(s_msysFileMng.pData, 8) : 0;
        IconInfo.pEmb = (s_msysFileMng.pData != 0 && s_msysFileMng.bRead != 0)
                            ? (unsigned char *)DF_MDATA_GetData(s_msysFileMng.pData, 9) : 0;
        IconInfo.pCom = 0;
        return 0;
    default:
        return 1;
    }
}

/* nobody has loaded a game yet (gLoadCheckF: __LoadMcGameSystem sets it, __PlaySystem's Save item reads it) */
void DF_InitMemCard(void)
{
    gLoadCheckF[0] = 0;
    gLoadCheckF[1] = 0;
    gLoadCheckF[2] = 0;
    gLoadCheckF[3] = 0;
    if (hook_init_mem_card) hook_init_mem_card();   /* port hook */
}

/* ---- group 8: the System top menu (Save / Load / Emblem / Options / Exit; Versus garage: Save / Load / Options) ---- */

typedef struct struct_4550d8c9 SysMenu;   /* System menu (dt) */

static inline SysMenu *msys_sys(struct stMobj *o) { return (SysMenu *)&o->dt; }

/* help string ids per menu item (dt.paStrid): OLMENU rodata in the hybrid (dt keeps the guest address) */
#ifdef ACNB_SOURCEPORT
static int msys_strid_normal[6] = {0x192e5, 0x192e6, 0x192e7, 0x192e8, 0x192e9, 0x192ea};
static int msys_strid_vs[3] = {0x192e5, 0x192e6, 0x192e9};
#define MSYS_STRID_NORMAL msys_strid_normal
#define MSYS_STRID_VS msys_strid_vs
#else
#define MSYS_STRID_NORMAL g_aMUnitSystemStridForNormal
#define MSYS_STRID_VS g_aMUnitSystemStridForVs
#endif

/* inlined 3x: back to the caller (release the model file if the System menu loaded it, restore MenuWorkp) */
static void msys_sys_end(struct stMobj *ioObjp)
{
    SysMenu *dt = msys_sys(ioObjp);
    DF_MBACK_PopSceneLabel();
    if (s_msysFileMng.Start == 0xa) {
        if (s_msysFileMng.pData != 0 && s_msysFileMng.bRead != 0) DF_MMM_Free(0, s_msysFileMng.pData);
        s_msysFileMng.bRead = 0;
        s_msysFileMng.pData = 0;
        s_msysFileMng.Step = 0;
        IconInfo.pGame = 0;
        IconInfo.pEmb = 0;
        IconInfo.pCom = 0;
    }
    MenuWorkp = (unsigned char *)dt->MenuWorkp_save;
    ioObjp->stat = 5;
    ioObjp->play_func = 0;
}

/* inlined 3x: whole-menu fade out (BGM too outside the Versus garage), then __PlaySystemExitFadeOut */
static void msys_sys_fade(struct stMobj *ioObjp)
{
    SysMenu *dt = msys_sys(ioObjp);
    DF_MBGUIDE_Hide();
    DF_MBACK_SetFadeOutNX();   /* the asm passes a0 = 0; DWARF has no parameter */
    DF_MENU_SetFadeOut(0x1e);
    if (gMcMode != 1) Bgm_FadeOutStart();
    dt->step = 0;
    ioObjp->count = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = (void (*)(void))GFN(__PlaySystemExitFadeOut);
}

/* fade out the menu units and the help line, then __PlaySystemFadeOut */
static void __SetSystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    SysMenu *dt = msys_sys(ioObjp);
    if (dt->pMobjUnitSystem) MUnitSystem_FadeOutStart(dt->pMobjUnitSystem, 0xd);
    if (dt->mode == 0) {
        if (dt->kindFade == 9) {   /* top units belong to the caller: only on decide */
            MUnitTopBase_FadeOutStart(dt->pMobjUnitTopBase, 9);
            MUnitTopSystem_FadeOutStart(dt->pMobjUnitTopSystem, 9);
        }
    } else {
        MUnitTopBase_FadeOutStart(dt->pMobjUnitTopBase, 0xd);
        MUnitTopSystem_FadeOutStart(dt->pMobjUnitTopSystem, 0xd);
    }
    DF_M1HELP_FadeOut();
    dt->step = 0;
    ioObjp->count = 0;
    ioObjp->stat = 2;
    ioObjp->play_func = (void (*)(void))GFN(__PlaySystemFadeOut);
}

/* Exit item (single player): "save first?" -> MC shell in save mode; else "quit?" -> fade out / back to the menu */
static void __PlaySystemExitSaveGame(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    PS2_STACK(int, result, 0x3c);
    SysMenu *dt = msys_sys(ioObjp);
    switch (dt->step) {
    case 0:
        msys_dlg(2, 0x30e38, 0, 0);
        dt->step++;
        break;
    case 1:
        if (__SyncDialog(&result) < 4) return;
        if (result == 1) {
            dt->bExitSave = 1;
            dt->csysp = __NewMcSystem(ioObjp, dt->player, 0, 0, 0, 0);
            __SetSystemFadeOut(ioObjp);
        } else {
            msys_dlg(2, 0x30e39, 1, 0);
            dt->step++;
        }
        break;
    case 2:
        if (__SyncDialog(&result) < 4) return;
        if (result == 1) {
            MenuMng.stat = 3;
            __SetSystemFadeOut(ioObjp);
            dt->bFadeOut = 1;
        } else {
            ioObjp->play_func = (void (*)(void))GFN(__PlaySystem);
        }
        break;
    }
}

/* Exit item (Versus garage; the 3-item menu never produces it): "quit?" -> fade out / back to the menu */
static void __PlaySystemExitGame(struct stMobj *ioObjp)
{
    PS2_FRAME(0x40);
    PS2_STACK(int, result, 0x3c);
    SysMenu *dt = msys_sys(ioObjp);
    switch (dt->step) {
    case 0:
        msys_dlg(2, 0x30e3a, 1, 0);
        dt->step++;
        break;
    case 1:
        if (__SyncDialog(&result) < 4) return;
        if (result == 1) {
            MenuMng.stat = 3;
            __SetSystemFadeOut(ioObjp);
            dt->bFadeOut = 1;
        } else {
            ioObjp->play_func = (void (*)(void))GFN(__PlaySystem);
        }
        break;
    }
}

/* a child screen (csysp) runs: when it ends, quit / fade out or come back to the menu */
static void __PlaySystemDispatch(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    SysMenu *dt = msys_sys(ioObjp);
    struct stMobj *cs = dt->csysp;
    /* original reads csysp->stat before its NULL test (PS2 address 2) */
    unsigned char st = cs ? cs->stat : ((const unsigned char *)PS2_NULL_MEM())[2];
    if (st != 5) return;
    if (cs) {
        msys_erase(cs);
        dt->csysp = 0;
    }
    if (gMcMode != 1 && MenuMng.stat != 4 && ((unsigned int)(int)dt->select < 3 || dt->select == 5))
        DF_MBACK_PopSceneLabel();
    if (MenuMng.stat == 4) {
        if (gMcMode == 1) msys_sys_end(ioObjp);
        else msys_sys_fade(ioObjp);
    } else if (dt->bExitSave != 0 && s_msysMcResult == 1) {
        MenuMng.stat = 3;
        msys_sys_fade(ioObjp);
    } else {
        dt->kindFade = 5;
        ioObjp->stat = 1;
        ioObjp->play_func = (void (*)(void))GFN(__PlaySystemFadeIn);
        ioObjp->count = 0;
        dt->step = 0;
    }
}

/* wait for the menu fade (30 frames), then back to the caller */
static void __PlaySystemExitFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    SysMenu *dt = msys_sys(ioObjp);
    switch (dt->step) {
    case 0:
        ioObjp->count++;
        if (ioObjp->count >= 0x1e) dt->step++;
        break;
    case 1:
        msys_sys_end(ioObjp);
        break;
    }
}

/* delete the menu units (after 5 frames, once they stopped), then run the child / fade out / return */
static void __PlaySystemFadeOut(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    SysMenu *dt = msys_sys(ioObjp);
    switch (dt->step) {
    case 0:
        ioObjp->count++;
        if (ioObjp->count < 5) return;
        if (dt->pMobjUnitSystem) {
            if (MUnitSystem_IsMove(dt->pMobjUnitSystem)) return;
            MUnitSystem_Cleanup(dt->pMobjUnitSystem);
            if (dt->pMobjUnitSystem) {
                msys_erase(dt->pMobjUnitSystem);
                dt->pMobjUnitSystem = 0;
            }
        }
        if (dt->mode == 1) {   /* the Versus garage menu owns its top units (no NULL tests on the Cleanup calls) */
            MUnitTopBase_Cleanup(dt->pMobjUnitTopBase);
            MUnitTopSystem_Cleanup(dt->pMobjUnitTopSystem);
            if (dt->pMobjUnitTopBase) {
                msys_erase(dt->pMobjUnitTopBase);
                dt->pMobjUnitTopBase = 0;
            }
            if (dt->pMobjUnitTopSystem) {
                msys_erase(dt->pMobjUnitTopSystem);
                dt->pMobjUnitTopSystem = 0;
            }
        }
        dt->step++;
        break;
    case 1:
        if (dt->csysp == 0) {
            if (dt->bFadeOut) msys_sys_fade(ioObjp);
            else msys_sys_end(ioObjp);
        } else {
            ioObjp->stat = 3;
            ioObjp->play_func = (void (*)(void))GFN(__PlaySystemDispatch);
        }
        break;
    }
}

/* idle: key guide, cancel, decide (start the item's screen), cursor up / down */
static void __PlaySystem(struct stMobj *ioObjp)
{
    PS2_FRAME(0x60);
    SysMenu *dt = msys_sys(ioObjp);
    MPadUser = dt->player;   /* every frame, before the key-guide test */
    if (MenuKeyGuide_IsShown()) return;
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    if (pad->start == 1) {
        MenuKeyGuide_Show(dt->keyguide);
        return;
    }
    if (pad->Rrt == 1) {
        dt->kindFade = 0xd;
        Voice_Call(0x3eb, 0x7f);
        __SetSystemFadeOut(ioObjp);
        return;
    }
    if (pad->Rdw == 1) {
        dt->kindFade = 9;
        Voice_Call(0x3ea, 0x7f);
        int sel = -1;
        if (gMcMode == 1) {   /* Save / Load / Options */
            int idx = dt->idx;
            if (idx == 2) sel = 4;
            else if (idx == 1) sel = 1;
            else if (idx == 0) sel = 0;
        } else if ((unsigned int)dt->idx < 6) {
            sel = dt->idx;
        }
        switch (sel) {
        case 0:   /* Save */
            dt->bExitSave = 0;
            if (gMcMode == 1 && gLoadCheckF[(int)dt->player] == 0) {
                /* Versus: only data loaded before can be saved (select is not stored) */
                dt->step = 0;
                ioObjp->play_func = (void (*)(void))GFN(__DlgSystemDisableLoad);
                return;
            }
            /* fall through */
        case 1:
        case 2:
            dt->csysp = __NewMcSystem(ioObjp, dt->player, sel, 0, 0, 0);
            __SetSystemFadeOut(ioObjp);
            break;
        case 3: {   /* Emblem load: the title-style selector object (no model-file reset here) */
            char p = dt->player;
            struct stMobj *o = DF_MOBJ_GetNewObject(ioObjp);
            if (o) {
                o->type = 0;
                o->stat = 1;
                o->play_func = (void (*)(void))GFN(__PlayEmblemLoadSystemFadeIn);
                o->pkt_func = (void (*)(void))GFN(__PlayEmblemLoadSystemPkt);
                o->count = 0;
                msys_title(o)->player = p;
                DF_MBACK_PushSceneLabel((enum_5124d09c)0x4a);
            } else {
                o = 0;
            }
            dt->csysp = o;
            __SetSystemFadeOut(ioObjp);
            break;
        }
        case 4:   /* Options */
            __SetSystemFadeOut(ioObjp);
            dt->csysp = MenuSysOption_New(ioObjp, dt->player, dt->mode, 0);
            break;
        case 5:   /* Exit */
            dt->step = 0;
            ioObjp->play_func = gMcMode == 1 ? (void (*)(void))GFN(__PlaySystemExitGame)
                                             : (void (*)(void))GFN(__PlaySystemExitSaveGame);
            break;
        case 6:   /* dead: the menu never produces 6 */
            dt->bExitSave = 0;
            dt->csysp = __NewMcSystem(ioObjp, dt->player, sel, 0, 0, 0);
            __SetSystemFadeOut(ioObjp);
            break;
        }
        dt->select = (char)sel;
        return;
    }
    struct struct_81b11733 *rep = &MPad[MPadUser].repeat;
    int moved = 0;
    if (rep->dw) {
        dt->idx++;
        moved = 1;
        if (dt->idx >= dt->idx_max) dt->idx = 0;
    } else if (rep->up) {
        moved = 1;
        dt->idx--;
        if (dt->idx < 0) dt->idx = dt->idx_max - 1;
    }
    if (!moved) return;
    Voice_Call(0x3e9, 0x7f);
    DF_M1HELP_ChangeStringId(dt->paStrid[dt->idx]);
    MUnitSystem_ChangeCursor(dt->pMobjUnitSystem, dt->idx);   /* asm passes a2 = 0: unused (DWARF drops it) */
}

/* 5 frames, camera + help, create / fade in the menu units, wait for the camera */
static void __PlaySystemFadeIn(struct stMobj *ioObjp)
{
    PS2_FRAME(0x30);
    SysMenu *dt = msys_sys(ioObjp);
    switch (dt->step) {
    case 0:
        ioObjp->count++;
        if (ioObjp->count < 5) return;
        DF_MVIEW_MotionStart(MViewp, 0xaa);
        DF_M1HELP_SetStringId(dt->paStrid[dt->idx]);
        DF_M1HELP_FadeIn();
        DF_MBGUIDE_Show();
        dt->step++;
        break;
    case 1:
        if (!dt->pMobjUnitSystem) dt->pMobjUnitSystem = MUnitSystem_NewSystem(ioObjp, (char)(gMcMode == 1 ? 2 : 1));
        MUnitSystem_FadeInStart(dt->pMobjUnitSystem, 1);
        if (dt->mode == 0) {
            if (dt->kindFade == 5) {   /* back from a child screen: the caller's top units fade in again */
                MUnitTopBase_FadeInStart(dt->pMobjUnitTopBase, 5, 1);
                MUnitTopSystem_FadeInStart(dt->pMobjUnitTopSystem, 5);
            }
        } else {   /* Versus garage: own top units, recreated on every entry */
            dt->pMobjUnitTopBase = MUnitTopBase_NewSystem(ioObjp, 2);
            dt->pMobjUnitTopSystem = MUnitTopSystem_NewSystem(ioObjp, 2, dt->player);
            MUnitTopBase_FadeInStart(dt->pMobjUnitTopBase, 1, 1);
            MUnitTopSystem_FadeInStart(dt->pMobjUnitTopSystem, 1);
        }
        MUnitSystem_ChangeCursor(dt->pMobjUnitSystem, dt->idx);
        dt->step++;
        break;
    case 2:
        if (DF_MVIEW_MotionSync(MViewp)) {
            ioObjp->stat = 0;
            ioObjp->play_func = (void (*)(void))GFN(__PlaySystem);
        }
        break;
    }
}

/* empty draw_func */
static void __PlaySystemDraw(void)
{
}

/* System menu object: inMode 1 = Versus garage (3 items, own top units), 0 = single player top (6 items) */
struct stMobj *DF_MSYS_NewSystem(struct stMobj *inObjp, int inMode, int inPlayer, struct stMobj *pMobjUnitTopBase,
                                 struct stMobj *pMobjUnitTopSystem)
{
    PS2_FRAME(0x60);
    struct stMobj *o = DF_MOBJ_GetNewObject(inObjp);
    if (!o) return 0;
    SysMenu *dt = msys_sys(o);
    o->type = 0;
    o->stat = 1;
    dt->MenuWorkp_save = MenuWorkp;
    dt->player = (char)inPlayer;
    dt->mode = (char)inMode;
    gMcMode = (signed char)inMode;
    dt->pMobjUnitTopBase = pMobjUnitTopBase;
    dt->pMobjUnitTopSystem = pMobjUnitTopSystem;
    if (gMcMode == 1) {
        dt->idx_max = 3;
        dt->keyguide = 0x3d;
        dt->paStrid = MSYS_STRID_VS;
    } else {
        dt->idx_max = 6;
        dt->keyguide = 0x3e;
        dt->paStrid = MSYS_STRID_NORMAL;
    }
    o->play_func = (void (*)(void))GFN(__PlaySystemFadeIn);
    o->draw_func = (void (*)(void))GFN(__PlaySystemDraw);
    o->pkt_func = 0;
    o->count = 0;
    DF_MBACK_PushSceneLabel((enum_5124d09c)0x46);
    s_msysFileMng.bRead = 0;
    s_msysFileMng.pData = 0;
    s_msysFileMng.Start = 0xa;
    s_msysFileMng.Step = 0;
    IconInfo.pGame = 0;
    IconInfo.pEmb = 0;
    IconInfo.pCom = 0;
    return o;
}
