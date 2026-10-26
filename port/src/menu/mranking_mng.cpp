/* src/menu/mranking_mng.c - the ranker AC viewer behind the arena ranking screens (OLMENU): reads one ranker's AC
 * (and emblem) and shows it in the menu's M_ACTable[0] slot.
 * MRankingMng_NewSystem makes the manager mobj, snapshots M_ACTable[0] and both EmblemTM2 buffers (restored by
 * __MRankingMng_Finish), and a child "ReadAC" mobj (stMenuRankingACRead) whose play function __MRankingMng_ReadPlay
 * runs the read state machine through MenuRankingReadFuncs[ReadFuncMode]: ReadCancel (0) -> PartslistReadByID (1,
 * starts the asynchronous emblem File_Read) -> EmblemReadWait (4: builds the AC with MakeACData from the asem data
 * record, starts the model load) -> PartsReadWait (6: waits on the AC's read_func callback, then copies the AC into
 * M_ACTable[0], swaps the two AC buffers and makes the emblem sprite with EmblemNew). The parent screen only calls
 * SetNextReadID / SetShow / ChangeEmblemMode / IsFinishReading / Cleanup. The emblem sprite mobj fades itself in /
 * out and erases itself (__MRankingMng_PlayEmblem).
 * Whole file native. Copy-paste code is kept as the original wrote it: MakeACData's 19 TrueID2Index calls (one
 * repeated), the three identical "read the emblem" bodies, Finish's repeated guarded frees. */
#include <string.h>
#include "acnb/sdk.h"

/* manager mobj work (no DWARF name; offsets from the asm) */
struct __attribute__((packed, aligned(4))) MRankingMngWork {
    struct stMobj *pParentUnused; /* 0x0 (never written here) */
    struct stMobj *pReadAC;       /* 0x4 */
    unsigned char _08[0xc];
    struct stMenuAC *pBackupAC;   /* 0x14: copy of *M_ACTable[0] */
    char *pBackupEmb0;            /* 0x18: copy of EmblemTM2[0] */
    char *pBackupEmb1;            /* 0x1c: copy of EmblemTM2[1] */
    char *pAsemData;              /* 0x20: AcAssemblyPartsSt records, stride 0x120 */
    char mode;                    /* 0x24: 1 training, 2 official / search, 4 ranking */
    char emblemMode;              /* 0x25: 0 = emblem shown */
    unsigned char _26[2];
    int wait;                     /* 0x28: IsFinishReading */
};

/* emblem sprite mobj work (no DWARF name; memset 0x20 by EmblemNew) */
struct __attribute__((packed, aligned(4))) MRankingEmblemWork {
    struct stMobj *pParent; /* 0x0: the manager mobj */
    struct stMobj *pSprite; /* 0x4 */
    int _08;
    int keep;               /* 0xc: 1 at creation; 0 = fade out and erase */
    int _10;
    int tex;                /* 0x14: MenuTex handle */
};

#define MRK_MNG(p) ((struct MRankingMngWork *)&(p)->dt)
#define MRK_RD(p) ((struct stMenuRankingACRead *)&(p)->dt)
#define MRK_EMB(p) ((struct MRankingEmblemWork *)&(p)->dt)
#define MRK_MODE(w) (*(unsigned char *)&(w)->ReadFuncMode)

/* MenuRankingReadFuncs (OLMENU 0x1edff90), by ReadFuncMode */
static void *const s_mrk_read_funcs[9] = {
    (void *)GFN(__MRankingMng_ReadCancel),     (void *)GFN(__MRankingMng_PartslistReadByID),
    (void *)GFN(__MRankingMng_PartslistReadWait), (void *)GFN(__MRankingMng_EmblemRead),
    (void *)GFN(__MRankingMng_EmblemReadWait), (void *)GFN(__MRankingMng_PartsRead),
    (void *)GFN(__MRankingMng_PartsReadWait),  (void *)GFN(__MRankingMng_PartslistRead),
    (void *)GFN(__MRankingMng_ReadWait),
};

/* f_draw = 0, f_erase = 1 (two read-modify-writes of byte 0) */
static inline void mrk_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

/* emblem fade-out start (ChangeEmblemMode / SetShow) */
static inline void mrk_emblem_out(struct stMobj *e)
{
    e->stat = 2;
    e->count = 0;
    DF_MOBJ_SetFadeOut(MRK_EMB(e)->pSprite);
}

/* emblem fade-in start; shown only while the manager's emblemMode is 0 */
static inline void mrk_emblem_in(struct stMobj *e)
{
    e->stat = 1;
    e->count = 0;
    if (MRK_MNG(MRK_EMB(e)->pParent)->emblemMode == 0) {
        DF_MOBJ_SetDrawEnable(MRK_EMB(e)->pSprite, 1);
        DF_MOBJ_SetFadeIn(MRK_EMB(e)->pSprite);
    }
}

/* the emblem read body (EmblemRead, inlined in PartslistReadWait / PartslistReadByID): player emblem, a COM
   ranker's emblem file (asynchronous), or none */
static inline void mrk_read_emblem(struct stMenuRankingACRead *w, struct MRankingMngWork *mng)
{
    int id = w->rankReadID;
    if (id < 0 && mng->mode != 3)
        memcpy(EmblemTM2[1], w->PlayerEmb, 0x4420);
    else if (id >= 0x190)
        File_Read(id + 0x1bbd, -1, EmblemTM2[1], 0);
    else
        memset(EmblemTM2[1], 0, 0x4420);
}

/* the "set the motion id of a COM ranker" step (ReadCancel / ReadPlay) */
static inline void mrk_set_ranker_motion(struct stMenuRankingACRead *w, int id)
{
    if (id >= 0x190) {
        MenuMng.idRankerMotion = (char)(id - 0x18f);
        w->ReadAC->flag.set_mi2 = 0;
    } else {
        MenuMng.idRankerMotion = 0;
    }
}

/* emblem mobj play: stat 1 fade in, 2 fade out (then unregister + erase when not kept), 0 idle (fade out once the
   keep flag drops) */
static void __MRankingMng_PlayEmblem(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct MRankingEmblemWork *w = MRK_EMB(pMobj);
    switch (pMobj->stat) {
    case 1:
        pMobj->count++;
        if (pMobj->count < 7) return;
        pMobj->stat = 0;
        pMobj->count = 0;
        return;
    case 2:
        pMobj->count++;
        if (pMobj->count < 7) return;
        pMobj->stat = 0;
        pMobj->count = 0;
        if (w->keep != 0) return;
        MenuTex_UnRegisterSingleTex(w->tex);
        if (w->pSprite) {
            mrk_erase(w->pSprite);
            w->pSprite = 0;
        }
        if (pMobj) mrk_erase(pMobj);
        return;
    case 0:
        if (w->keep != 0) return;
        DF_MOBJ_SetFadeOut(w->pSprite);
        pMobj->stat = 2;
        return;
    }
}

/* (re)makes the emblem sprite of the ReadAC mobj from EmblemTM2[1] (copied to EmblemTM2[0] first) */
static struct stMobj *__MRankingMng_EmblemNew(struct stMobj *pMobj)
{
    PS2_FRAME(0x90);
    PS2_STACK(int[4], uv, 0x60);
    PS2_STACK(struct stMenuRect, rect, 0x70);
    PS2_STACK(struct struct_76b810ec, col, 0x8c);
    struct stMenuRankingACRead *rd = MRK_RD(pMobj);
    if (rd->pMobjEmblem == 0) rd->pMobjEmblem = DF_MOBJ_GetNewObject(pMobj);
    struct MRankingEmblemWork *ew = MRK_EMB(rd->pMobjEmblem);
    memset(ew, 0, 0x20);
    ew->pParent = rd->pParent;
    ew->keep = 1;
    rd->pMobjEmblem->play_func = (void (*)(void))GFN(__MRankingMng_PlayEmblem);
    struct stMobj *e = rd->pMobjEmblem;
    struct MRankingMngWork *mng = MRK_MNG(MRK_EMB(e)->pParent);
    rect.x = 0x206; rect.y = 0x124; rect.w = 0x60; rect.h = 0x60; /* OLMENU 0x1edffc0 */
    uv[0] = 0; uv[1] = 0; uv[2] = 0x80; uv[3] = 0x80;              /* OLMENU 0x1edffd0 */
    col.r = 0x80; col.g = 0x80; col.b = 0x80; col.a = 0x80;        /* main 0x27bd40 */
    memcpy(EmblemTM2[0], EmblemTM2[1], 0x4420);
    if (ew->tex) MenuTex_UnRegisterSingleTex(ew->tex);
    ew->tex = MenuTex_RegisterSingleTex((struct struct_c53f69de *)EmblemTM2[0]);
    ew->pSprite = DF_MOBJ_NXNewSprite(e, ew->tex, &rect, uv, &col, 0, 0, 0, 0);
    if (mng->emblemMode == 0) {
        e->stat = 1;
        e->count = 0;
        /* the same byte tested again through the emblem's parent */
        if (MRK_MNG(MRK_EMB(e)->pParent)->emblemMode == 0) {
            DF_MOBJ_SetDrawEnable(ew->pSprite, 1);
            DF_MOBJ_SetFadeIn(ew->pSprite);
        }
    } else {
        DF_MOBJ_SetDrawEnable(ew->pSprite, 0);
    }
    return rd->pMobjEmblem;
}

/* builds ReadAC from the CopyAC template + asem record cur (cur < 0: the mission AC in g_nextstage) */
static void __MRankingMng_MakeACData(struct stMenuRankingACRead *pWorkACRead, int cur)
{
    PS2_FRAME(0xf0);
    PS2_STACK(struct struct_098975f3, color, 0x40);
    struct MRankingMngWork *mng = MRK_MNG(pWorkACRead->pParent);
    struct stMenuAC *ac = pWorkACRead->ReadAC;
    memcpy(ac, pWorkACRead->CopyAC, 0x3e0);
    struct AcAssemblyPartsSt *wpp;
    if (cur >= 0) {
        wpp = (struct AcAssemblyPartsSt *)(mng->pAsemData + cur * 0x120);
    } else {
        DF_M_AC_MissionTransStart();
        wpp = &g_nextstage.asmbly[0];
    }
    ac->part_id[0] = DF_MPART_TrueID2Index(0, wpp->idParts[0]);
    ac->part_id[1] = DF_MPART_TrueID2Index(1, wpp->idParts[1]);
    ac->part_id[2] = DF_MPART_TrueID2Index(2, wpp->idParts[2]);
    ac->part_id[2] = DF_MPART_TrueID2Index(2, wpp->idParts[2]);
    ac->part_id[5] = DF_MPART_TrueID2Index(3, wpp->idParts[3]);
    ac->part_id[6] = DF_MPART_TrueID2Index(4, wpp->idParts[4]);
    ac->part_id[7] = DF_MPART_TrueID2Index(5, wpp->idParts[5]);
    ac->part_id[8] = DF_MPART_TrueID2Index(6, wpp->idParts[6]);
    ac->part_id[9] = DF_MPART_TrueID2Index(7, wpp->idParts[7]);
    ac->part_id[10] = DF_MPART_TrueID2Index(8, wpp->idParts[8]);
    ac->part_id[11] = DF_MPART_TrueID2Index(8, wpp->idParts[8]);
    ac->part_id[12] = DF_MPART_TrueID2Index(9, wpp->idParts[9]);
    ac->part_id[13] = DF_MPART_TrueID2Index(9, wpp->idParts[9]);
    ac->part_id[14] = DF_MPART_TrueID2Index(0xa, wpp->idParts[10]);
    ac->part_id[15] = DF_MPART_TrueID2Index(0xa, wpp->idParts[11]);
    ac->part_id[16] = DF_MPART_TrueID2Index(0xb, wpp->idParts[12]);
    ac->part_id[17] = DF_MPART_TrueID2Index(0xc, wpp->idParts[13]);
    ac->part_id[18] = DF_MPART_TrueID2Index(0xb, wpp->idParts[14]);
    ac->part_id[19] = DF_MPART_TrueID2Index(0xc, wpp->idParts[15]);
    DF_M_AC_PARTS_COLOR_STtoMENU_AC_COLOR_ST(&color, wpp->color);
    DF_M_AC_UpdateWepColor(ac, &color, 1);
    DF_M_AC_UpdateClut(ac, &color, 1);
    ac->camouflage[0] = wpp->color[0].camouflage;
    ac->camouflage[1] = wpp->color[1].camouflage;
    ac->camouflage[2] = wpp->color[2].camouflage;
    ac->camouflage[3] = wpp->color[4].camouflage;
}

/* state 8: idle */
static void __MRankingMng_ReadWait(void)
{
}

/* state 7 */
static void __MRankingMng_PartslistRead(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct stMenuRankingACRead *w = MRK_RD(pMobj);
    if (!File_IsAsyncBusy()) MRK_MODE(w) = 8;
}

/* state 6: wait for the AC model load, then show it (fade in), swap the AC buffers and make the emblem */
static void __MRankingMng_PartsReadWait(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct stMenuRankingACRead *w = MRK_RD(pMobj);
    struct stMenuAC *ac = w->ReadAC;
    struct MRankingMngWork *mng = MRK_MNG(w->pParent);
    if (ac->read_func) {
        gcall_as<int(struct stMenuAC *)>(ac->read_func, ac); /* result dropped */
        return;
    }
    MRK_MODE(w) = 7;
    if (w->PastAC) DF_M_AC_Reset2(w->PastAC);
    if (mng->mode == 2 || mng->mode == 4) MenuMng.f_no_draw_spare = (w->rankReadID >= 0x190) ? 1 : 0;
    memcpy(M_ACTable[0], w->ReadAC, 0x3e0);
    M_ACTable[0]->pos[2] = M_ACTable[0]->pos[2] + 4.5f;
    DF_M_AC_SetFadeIn(M_ACTable[0], 0xf, -0x1.333334p-2f /* -0.3f, 0xbe99999a */);
    w->PastAC = w->ReadAC;
    w->ReadAC = (w->ReadAC == w->ACBuff[0]) ? w->ACBuff[1] : w->ACBuff[0];
    memset(w->ReadAC, 0, 0x3e0);
    if ((w->rankReadID < 0 || w->rankReadID >= 0x190) && mng->mode != 3)
        __MRankingMng_EmblemNew(pMobj);
    else
        memcpy(EmblemTM2[0], EmblemTM2[1], 0x4420);
}

/* state 5: build the AC and start its model load */
static void __MRankingMng_PartsRead(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct stMenuRankingACRead *w = MRK_RD(pMobj);
    __MRankingMng_MakeACData(w, w->rankReadID);
    DF_M_AC_PartsRead(w->ReadAC);
    DF_M_AC_SetData(w->ReadAC);
    DF_M_AC_SetFadeOut(M_ACTable[0], 5, 0.0f);
    MRK_MODE(w) = 6;
    __MRankingMng_PartsReadWait(pMobj);
}

/* state 4: once the emblem read is done, the PartsRead body */
static void __MRankingMng_EmblemReadWait(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct stMenuRankingACRead *w = MRK_RD(pMobj);
    if (File_IsAsyncBusy()) return;
    MRK_MODE(w) = 5;
    __MRankingMng_MakeACData(w, w->rankReadID);
    DF_M_AC_PartsRead(w->ReadAC);
    DF_M_AC_SetData(w->ReadAC);
    DF_M_AC_SetFadeOut(M_ACTable[0], 5, 0.0f);
    MRK_MODE(w) = 6;
    __MRankingMng_PartsReadWait(pMobj);
}

/* state 3: start the emblem read */
static void __MRankingMng_EmblemRead(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct stMenuRankingACRead *w = MRK_RD(pMobj);
    struct MRankingMngWork *mng = MRK_MNG(w->pParent);
    if (File_IsAsyncBusy()) return;
    mrk_read_emblem(w, mng);
    MRK_MODE(w) = 4;
}

/* state 2: = state 1 (both run the EmblemRead body inline) */
static void __MRankingMng_PartslistReadWait(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct stMenuRankingACRead *w = MRK_RD(pMobj);
    if (File_IsAsyncBusy()) return;
    MRK_MODE(w) = 3;
    struct MRankingMngWork *mng = MRK_MNG(w->pParent);
    if (File_IsAsyncBusy()) return;
    mrk_read_emblem(w, mng);
    MRK_MODE(w) = 4;
}

/* state 1 */
static void __MRankingMng_PartslistReadByID(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct stMenuRankingACRead *w = MRK_RD(pMobj);
    if (File_IsAsyncBusy()) return;
    MRK_MODE(w) = 3;
    struct MRankingMngWork *mng = MRK_MNG(w->pParent);
    if (File_IsAsyncBusy()) return;
    mrk_read_emblem(w, mng);
    MRK_MODE(w) = 4;
}

/* state 0: take the next id once the file system is idle */
static void __MRankingMng_ReadCancel(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct stMenuRankingACRead *w = MRK_RD(pMobj);
    struct MRankingMngWork *mng = MRK_MNG(w->pParent);
    if (File_IsAsyncBusy()) return;
    w->rankReadID = w->nextReadID;
    MRK_MODE(w) = 1;
    if (mng->mode == 2 || mng->mode == 4) mrk_set_ranker_motion(w, w->nextReadID);
}

/* ReadAC mobj play: a new id cancels the running read (AC fade-out, emblem dropped), then the state function */
static void __MRankingMng_ReadPlay(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct stMenuRankingACRead *w = MRK_RD(pMobj);
    struct MRankingMngWork *mng = MRK_MNG(w->pParent);
    if (w->rankReadID != w->nextReadID) {
        if (mng->mode == 2 || mng->mode == 4) mrk_set_ranker_motion(w, w->nextReadID);
        if (MRK_MODE(w) != 0) {
            DF_M_AC_SetFadeOut(M_ACTable[0], 5, -0x1.333334p-2f /* -0.3f */);
            if (w->ReadAC->flag.set) {
                DF_M_AC_Reset2(w->ReadAC);
                MenuMng.f_use_readbuff = 0;
            }
            if (w->pMobjEmblem) {
                MRK_EMB(w->pMobjEmblem)->keep = 0;
                w->pMobjEmblem = 0;
            }
            MRK_MODE(w) = 0;
        }
    }
    gcall_as<void(struct stMobj *)>(s_mrk_read_funcs[MRK_MODE(w)], pMobj); /* no bound check */
}

/* makes the ReadAC child of the manager mobj */
static struct stMobj *__MRankingMng_ReadACNew(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct MRankingMngWork *mng = MRK_MNG(pMobj);
    struct stMobj *n = DF_MOBJ_GetNewObject(pMobj);
    struct stMenuRankingACRead *w = MRK_RD(n);
    w->pParent = pMobj;
    n->play_func = (void (*)(void))GFN(__MRankingMng_ReadPlay);
    w->ACBuff[0] = (struct stMenuAC *)DF_MMM_Alloc(0, 0x3e0);
    w->ACBuff[1] = (struct stMenuAC *)DF_MMM_Alloc(0, 0x3e0);
    w->ReadAC = w->ACBuff[0];
    w->PastAC = 0;
    w->PlayerEmb = mng->pBackupEmb0;
    DF_MFUNC_ClearMemory(w->ACBuff[0], 0xf8);
    DF_MFUNC_ClearMemory(w->ACBuff[1], 0xf8);
    w->CopyAC = mng->pBackupAC;
    w->rankReadID = w->nextReadID;
    MRK_MODE(w) = 1;
    w->pMobjEmblem = 0;
    return n;
}

/* manager mobj play: nothing */
static void __MRankingMng_Play(void)
{
}

/* frees the AC buffers, erases the ReadAC mobj, restores M_ACTable[0] / EmblemTM2 from the backups */
static void __MRankingMng_Finish(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct MRankingMngWork *mng = MRK_MNG(pMobj);
    struct stMenuRankingACRead *rd = MRK_RD(mng->pReadAC);
    DF_M_AC_Reset2(rd->ACBuff[0]);
    DF_M_AC_Reset2(rd->ACBuff[1]);
    MenuMng.f_use_readbuff = 0;
    DF_MMM_Free(0, rd->ACBuff[0]);
    rd->ACBuff[0] = 0;
    DF_MMM_Free(0, rd->ACBuff[1]);
    rd->ACBuff[1] = 0;
    if (rd->pMobjEmblem) {
        MRK_EMB(rd->pMobjEmblem)->keep = 0;
        rd->pMobjEmblem = 0;
    }
    if (mng->pReadAC) {
        mrk_erase(mng->pReadAC);
        mng->pReadAC = 0;
    }
    memcpy(M_ACTable[0], mng->pBackupAC, 0x3e0);
    memcpy(EmblemTM2[0], mng->pBackupEmb0, 0x4420);
    memcpy(EmblemTM2[1], mng->pBackupEmb1, 0x4420);
    FlushCache(0);
    DF_M_AC_UpdateClut(M_ACTable[0], &mng->pBackupAC->real_col, 1);
    DF_MMM_Free(0, mng->pBackupAC);
    mng->pBackupAC = 0;
    /* the original's free-and-clear macro, repeated */
    if (mng->pBackupEmb0) {
        DF_MMM_Free(0, mng->pBackupEmb0);
        mng->pBackupEmb0 = 0;
    }
    mng->pBackupEmb0 = 0;
    if (mng->pBackupEmb1) {
        DF_MMM_Free(0, mng->pBackupEmb1);
        mng->pBackupEmb1 = 0;
    }
    mng->pBackupEmb1 = 0;
    if (mng->pBackupAC) {
        DF_MMM_Free(0, mng->pBackupAC);
        mng->pBackupAC = 0;
    }
    mng->pBackupAC = 0;
    if (mng->pBackupEmb0) {
        DF_MMM_Free(0, mng->pBackupEmb0);
        mng->pBackupEmb0 = 0;
    }
    if (mng->pBackupEmb1) {
        DF_MMM_Free(0, mng->pBackupEmb1);
        mng->pBackupEmb1 = 0;
    }
    DF_MMD_ReleaseGeneralMMD();
    DF_MOBJ_Finish(pMobj);
}

/* -1 toggles, 0 shows, anything else hides the emblem */
void MRankingMng_ChangeEmblemMode(struct stMobj *pMobj, char modeEmblem)
{
    PS2_FRAME(0x20);
    struct MRankingMngWork *mng = MRK_MNG(pMobj);
    int show;
    if (modeEmblem == -1)
        show = (mng->emblemMode != 0);
    else
        show = (modeEmblem == 0);
    if (show) {
        mng->emblemMode = 0;
        struct stMobj *e = MRK_RD(mng->pReadAC)->pMobjEmblem;
        if (e) mrk_emblem_in(e);
    } else {
        mng->emblemMode = 1;
        struct stMobj *e = MRK_RD(mng->pReadAC)->pMobjEmblem;
        if (e) mrk_emblem_out(e);
    }
}

/* true once the file system has been idle for 10 calls */
char MRankingMng_IsFinishReading(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct MRankingMngWork *mng = MRK_MNG(pMobj);
    if (File_IsAsyncBusy()) {
        mng->wait = 0;
        return 0;
    }
    mng->wait++;
    return mng->wait >= 10;
}

void MRankingMng_SetNextReadID(struct stMobj *pMobj, int nextReadID)
{
    MRK_RD(MRK_MNG(pMobj)->pReadAC)->nextReadID = nextReadID;
}

void MRankingMng_Cleanup(struct stMobj *pMobj)
{
    MenuMng.idRankerMotion = 0;
    MenuMng.f_no_draw_spare = 0;
    __MRankingMng_Finish(pMobj);
}

/* AC + emblem fade in / out */
void MRankingMng_SetShow(struct stMobj *pMobj, char show)
{
    PS2_FRAME(0x20);
    struct stMenuRankingACRead *rd = MRK_RD(MRK_MNG(pMobj)->pReadAC);
    if (show == 0) {
        DF_M_AC_SetFadeOut(M_ACTable[0], 5, 0.0f);
        if (rd->pMobjEmblem) mrk_emblem_out(rd->pMobjEmblem);
    } else {
        DF_M_AC_SetFadeIn(M_ACTable[0], 0xf, 0.0f);
        if (rd->pMobjEmblem) mrk_emblem_in(rd->pMobjEmblem);
    }
}

struct stMobj *MRankingMng_NewSystem(struct stMobj *pParent, char mode, char *pAsemData)
{
    PS2_FRAME(0x40);
    struct stMobj *m = DF_MOBJ_GetNewObject(pParent);
    struct MRankingMngWork *w = MRK_MNG(m);
    w->pAsemData = pAsemData;
    w->mode = mode;
    m->play_func = (void (*)(void))GFN(__MRankingMng_Play);
    DF_MMD_SetGeneralMMD();
    w->pBackupAC = (struct stMenuAC *)DF_MMM_Alloc(0, 0x3e0);
    memcpy(w->pBackupAC, M_ACTable[0], 0x3e0);
    w->pBackupEmb0 = (char *)DF_MMM_Alloc(0, 0x4420);
    w->pBackupEmb1 = (char *)DF_MMM_Alloc(0, 0x4420);
    memcpy(w->pBackupEmb0, EmblemTM2[0], 0x4420);
    memcpy(w->pBackupEmb1, EmblemTM2[1], 0x4420);
    w->pReadAC = __MRankingMng_ReadACNew(m);
    DF_MGARAGE_SetMissionAc(0);
    return m;
}
