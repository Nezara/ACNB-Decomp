/* src/stage/stage.cpp - the stage (map) runtime. Stage_Load reads the compressed map file (CD id 10000 + 100 * map),
 * the "dmap" model files of destructible objects and characters, the event-script bundle and the merged map sound
 * bank; __AllocStageMemory allocates the StModel arrays (Map / Obj / Chr / Dobj and their shadow copies), the light /
 * fog / reverb / screen-effect tables and the gimmick / hit buffers; init_*_model bind the model data to the
 * StModels and build their matrices; Stage_Begin finishes (SysInfo copy, shadows, dummies, six ECM sources). Per
 * frame Stage_ExecMapScreenEffect cross-fades the screen-effect preset of the map region the player stands on,
 * STAGE_MAddLight re-derives the lit light table and Stage_ExecTransSpTexture swaps region textures. The rest are
 * accessors, the layer (group visibility) updater and marker-region searches. The texture upload / GIF list part of
 * Stage_Load and the env-ball packets are phase-2 render work (still the original calls). */
#include <stdint.h>
#include <string.h>
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

static inline StSortieConstant *stage_pc(void) { return g_sortie.pconstant; }

/* float -> u32 through the unsigned conversion */
static inline unsigned int stage_u32(float f)
{
    return !(2147483648.0f <= f) ? (unsigned int)ps2_ftoi(f) : (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u;
}

void *Stage_GetPgcBuf(void) { return s_pPgcBuf; }
void *Stage_GetPgvBuf(void) { return s_pPgvBuf; }
void *Stage_GetCmdBuf(void) { return s_pCmdBuf; }

/* point the event-data statics at the sections of the bundle (header entries at +0x20, 0x10 each: offset @4,
 * size @8; a zero size keeps the previous stage's pointer) */
static int __DistributeEventData(void)
{
    char *b = (char *)s_pEvDataBuf;
    void **dst[6] = {&s_pCmdBuf, &s_pPgvBuf, &s_pPgcBuf, &s_pMesBuf, &s_pScrBuf, &s_pPpgBuf};
    for (int k = 0; k < 6; k++) {
        char *e = b + 0x20 + 0x10 * k;
        if (*(int *)(e + 8)) *dst[k] = b + *(int *)(e + 4);
    }
    ScrTopP = (char *)s_pScrBuf;
    g_ScriptSize = *(unsigned int *)(b + 0x68);
    return 1;
}

/* region textures: uploaded once when the player enters the marker region */
void Stage_ExecTransSpTexture(void)
{
    PS2_FRAME(0x40);
    if (!s_spttex.isEnable) return;
    for (int ii = 0; ii < 4; ii++) {
        if (!s_spttex.ptmxTrans[ii]) continue;
        if (AcCtrl_IsArriveDmy(0, s_spttex.idxMarker[ii], 12)) {
            if (!s_spttex.isInRegion[ii]) {
                s_spttex.isInRegion[ii] = 1;
                DF_ReadTMXPointer(s_spttex.ptmxTrans[ii], 1);
            }
        } else {
            s_spttex.isInRegion[ii] = 0;
        }
    }
}

void Stage_TransSpTexture(int id)
{
    DF_ReadTMXPointer(s_spttex.ptmxTrans[id], 1);
}

/* write the four screen-effect presets: the fading-in region's parameters scaled by its fade, plus the fading-out
 * region's scaled by its own (colours as two truncated ints, values rounded) */
static void __SetScreffParam(__pfx_intp_st *pFadeInPfx, __pfx_intp_st *pFadeOutPfx)
{
    PS2_FRAME(0x90);
    float fi = pFadeInPfx->fade, fo = pFadeOutPfx->fade;
    for (int index = 0; index < 4; index++) {
        const unsigned char *in = pFadeInPfx->pMapScreff ? (const unsigned char *)pFadeInPfx->pMapScreff + 0x18 * index
                                                         : (const unsigned char *)PS2_NULL_MEM();
        const unsigned char *out = pFadeOutPfx->pMapScreff
                                       ? (const unsigned char *)pFadeOutPfx->pMapScreff + 0x18 * index : 0;
        unsigned char type = in[0x17];
        if (type != ScrEff_GetPresetKind(index)) ScrEff_SetPreset(index, type);
        if (in[0x17] == 0) continue;
        StScreenEffectParam *p = ScrEff_GetPresetParam(index);
        for (int c = 0; c < 3; c++) {
            for (int k = 0; k < 4; k++) p->color[c][k] = ps2_ftoi(fi * (float)in[4 * c + k]);
            if (out)
                for (int k = 0; k < 4; k++) p->color[c][k] += ps2_ftoi(fo * (float)out[4 * c + k]);
        }
        for (int c = 0; c < 3; c++) {
            unsigned char b0 = in[0xc + 2 * c];
            p->alpha[c].a = b0 & 3;
            p->alpha[c].b = (b0 >> 2) & 3;
            p->alpha[c].c = (b0 >> 4) & 3;
            p->alpha[c].d = b0 >> 6;
            p->alpha[c].f = in[0xd + 2 * c];
        }
        for (int k = 0; k < 4; k++) {
            if (out) {
                float t = (float)in[0x12 + k] * fi;
                float u = (float)out[0x12 + k] * fo;
                t = t + u;
                p->value[k] = (unsigned char)stage_u32(0.5f + t);
            } else {
                p->value[k] = (unsigned char)stage_u32((float)in[0x12 + k] * fi);
            }
        }
        ScrEff_SetPresetStatus(index, 1);
    }
}

/* s_PfxIntp[0/1]: the fading-in (state 1) and fading-out (state 2) presets, [2]: the next one queued while a fade
 * runs; the fade steps 0.05 per frame */
void Stage_ExecMapScreenEffect(void)
{
    PS2_FRAME(0x10);
    __pfx_intp_st *P = s_PfxIntp;
    _map_model_list_st *ml = g_ac[0].mp->nowparent->mlist;
    if (!ml) ml = Map->mlist;
    StMapScrEffSt *scr = ml->pscreff;
    int id = ml->screffid;
    int first = 0;
    if (P[0].pfx_id == -1 && P[1].pfx_id == -1) {
        P[0].pfx_id = id;
        P[0].pMapScreff = scr;
        first = 1;
        P[0].fade = 1.0f;
        P[0].state = 0;
    }
    __pfx_intp_st *in = &P[0], *out = &P[1];
    if (P[0].state != 1 && P[1].state == 1) {
        in = &P[1];
        out = &P[0];
    }
    if (in->state == 0) {
        if (!first && in->pfx_id != id) {
            out->pfx_id = in->pfx_id;
            out->pMapScreff = in->pMapScreff;
            out->fade = 1.0f;
            out->state = 2;
            in->pfx_id = id;
            in->pMapScreff = scr;
            in->fade = 0.0f;
            in->state = 1;
        }
    } else if (P[2].pfx_id != id) {
        P[2].pfx_id = id;
        P[2].pMapScreff = scr;
        P[2].fade = 0.0f;
        in->state = 1;
    }
    if (in->state != 0) {
        in->fade = in->fade + ps2_u2f(0x3d4ccccd);
        out->fade = out->fade - ps2_u2f(0x3d4ccccd);
        if (!(in->fade <= 1.0f) || out->fade < 0.0f) {
            if (P[2].pfx_id == -1 || P[2].pfx_id == in->pfx_id) {
                out->pfx_id = -1;
                out->pMapScreff = 0;
                out->fade = 0.0f;
                out->state = 0;
                in->fade = 1.0f;
                in->state = 0;
            } else {
                out->pfx_id = in->pfx_id;
                out->pMapScreff = in->pMapScreff;
                out->fade = 1.0f;
                out->state = 2;
                in->pfx_id = P[2].pfx_id;
                in->pMapScreff = P[2].pMapScreff;
                in->fade = 0.0f;
                in->state = 1;
                P[2].pfx_id = in->pfx_id;
            }
        }
    }
    __SetScreffParam(in, out);
}

static inline void stage_layer_models(StModel *m, int cnt, const unsigned char *spr, int view)
{
    unsigned char bit = (unsigned char)(1 << view);
    for (int i = 0; i < cnt; i++, m++) {
        m->on_layer |= bit;
        if (((const signed char *)spr)[m->groupid] == 0) m->on_layer &= (unsigned char)~bit;
    }
}

/* camera moved to group `next`: flag it and its connected groups (scratchpad bytes) and set / clear the view's bit
 * in on_layer of every map, object and destructible model */
int DF_LayerConnectProc(int current, int next, int view)
{
    PS2_FRAME(0x50);
    if (next == current) return 0;
    unsigned char *g = Array->group[next].layer;
    int n = g[0x3f];
    unsigned char *spr = (unsigned char *)PS2_SPR(0);
    ZeroMemory32b(spr, 0x40);   /* (words: 0x100 bytes) */
    spr[next] = 1;
    for (int i = 0; i < n; i++) spr[g[i]] = 1;
    stage_layer_models(Map, Array->head.mnum, spr, view);
    stage_layer_models(Obj, Array->head.onum, spr, view);
    stage_layer_models(Dobj, Array->head.dnum, spr, view);
    return 1;
}

int STAGE_SightInOut(int dmy_id)
{
    PS2_FRAME(0x30);
    PS2_STACK(float[4], mn, 0x20);
    PS2_STACK(float[4], mx, 0x10);
    MarkerSt *m = &Array->marker[dmy_id];
    const float *lo = (const float *)(m->wk), *hi = (const float *)(m->wk + 0x10);
    mn[0] = m->loc[0] + lo[0];
    mn[1] = m->loc[1] + lo[1];
    mn[2] = m->loc[2] + lo[2];
    mn[3] = m->loc[3];
    mx[0] = m->loc[0] + hi[0];
    mx[1] = m->loc[1] + hi[1];
    mx[2] = m->loc[2] + hi[2];
    mx[3] = m->loc[3];
    mx[3] = 1.0f;
    mn[3] = 1.0f;
    return CamFunc_RegionSightInOut(mn, mx);
}

/* variadic in the original: argNum ids (cdmy[0..]) a region marker must carry; loc strictly inside its box (the
 * original copies argNum values into a 5-entry array unchecked; its only caller passes 2) */
int STAGE_IsInReg(float *loc, int argNum, int id0, int id1, int id2, int id3, int id4, int id5)
{
    int extra[6] = {id0, id1, id2, id3, id4, id5};
    int genID[6];
    for (int i = 0; i < argNum && i < 6; i++) genID[i] = extra[i];
    MarkerSt *m = s_pmkrDmyRegion;
    for (int n = s_numDmyRegion; n; n--, m++) {
        int i = 0;
        for (; i < argNum; i++)
            if (m->cdmy[i] != genID[i < 6 ? i : 5]) break;
        if (i < argNum) continue;
        const float *lo = (const float *)(m->wk), *hi = (const float *)(m->wk + 0x10);
        int a = 0;
        for (; a < 3; a++) {
            if (loc[a] <= m->loc[a] + lo[a]) break;
            if (!(loc[a] < m->loc[a] + hi[a])) break;
        }
        if (a == 3) return 1;
    }
    return 0;
}

/* nearest region marker in XZ (cdmy2 >= -128: only markers with that cdmy[0]) */
static inline MarkerSt *stage_nearest_region(float *loc, float *distp, int filter, int cdmy2)
{
    float best = 10000.0f;
    MarkerSt *near = 0;
    MarkerSt *m = s_pmkrDmyRegion;
    for (int n = s_numDmyRegion; n; n--, m++) {
        if (filter && m->cdmy[0] != cdmy2) continue;
        float x = m->loc[0] - loc[0], z = m->loc[2] - loc[2];
        float d = ps2_sqrt(x * x + z * z);
        if (d < best) {
            best = d;
            near = m;
        }
    }
    if (distp) *distp = best;
    return near;
}

MarkerSt *STAGE_GetNearestDmyRegForIndex(float *loc, float *distp, int cdmy2)
{
    return stage_nearest_region(loc, distp, 1, cdmy2);
}

MarkerSt *STAGE_GetNearestDmyReg(float *loc, float *distp)
{
    return stage_nearest_region(loc, distp, 0, 0);
}

/* the first contiguous run of region markers (type 2) */
void STAGE_InitDmyLocData(void)
{
    MarkerSt *mk = Array->marker;
    int i = 0;
    while (i < 0x80 && mk[i].type != 2) i++;
    if (i == 0x80) {
        s_pmkrDmyRegion = 0;
        s_numDmyRegion = 0;
        return;
    }
    s_pmkrDmyRegion = &mk[i];
    s_numDmyRegion = 1;
    for (i++; i < 0x80 && mk[i].type == 2; i++) s_numDmyRegion++;
}

/* nearest live enemy AC or enemy of another team within range (squared distances), for the radar */
StModel *DF_CALC_GetNearestEnemy(float *loc, float range, float *dist, int acid)
{
    unsigned short team = MyAC[acid].cflag & 0xf;
    float rr = range * range, best = 16777216.0f;
    StModel *res = 0;
    for (int i = 0; i < NowPlayerNum; i++) {
        if (i == acid) continue;
        if (g_ac[i].dead || g_ac[i].areaovered) continue;
        StModel *m = &MyAC[i];
        if (m->cflag & team) continue;
        float x = loc[0] - m->loc[0], y = loc[1] - m->loc[1], z = loc[2] - m->loc[2];
        float d2 = z * z + (x * x + y * y);
        if (d2 < rr && d2 < best) {
            res = m;
            best = d2;
        }
    }
    EnemySt *ep = Enemy;
    for (int i = 0; i < StgInfo.cnum; i++, ep++) {
        if (ep->nowhp == 0) continue;
        StModel *m = ep->mp;
        if (m->use & 4) continue;
        if (m->cflag & team) continue;
        float x = loc[0] - m->loc[0], y = loc[1] - m->loc[1], z = loc[2] - m->loc[2];
        float d2 = z * z + (x * x + y * y);
        if (d2 < rr && d2 < best) {
            res = ep->mp;
            best = d2;
        }
    }
    if (!res) return 0;
    *dist = ps2_sqrt(best);
    return res;
}

MarkerSt *STAGE_GetMakerPointer(int index)
{
    return &Array->marker[index];
}

struct_441fa347 *Stage_GetMissionAcInfo(int index)
{
    return &s_paeChr->miac[index];
}

ArraySt *Stage_GetArrayPointer(int kind, int index)
{
    if (kind == 3) return &s_paeChr->arr[index];
    if (kind == 2) return &s_paeDobj->arr[index];
    if (kind == 1) return &Array->obj[index];
    if (kind == 0) return &Array->map[index];
    return 0;
}

StModel *STAGE_GetModelPointer(int type, int aid)
{
    switch ((unsigned int)type) {
    case 1: return &Map[aid];
    case 2: return &Obj[aid];
    case 3: return &Chr[aid];
    case 4: return g_ac[aid].mp;
    case 6: return &Itm[aid];
    case 8: return &Dobj[aid];
    default: return 0;
    }
}

/* lit light table: colours * mul + add, squared `post` times (dir[] of dest untouched, w kept); off = plain copy */
static void madd_light(_map_light_st *dest, _map_light_st *src, LightParamSt *lparam)
{
    PS2_FRAME(0x10);
    if (!lparam->on) {
        CopyMemory512b(dest, src, 0x100);   /* (16-byte units: 32 lights) */
        return;
    }
    for (int i = 0; i < 0x20; i++, dest++, src++) {
        for (int k = 0; k < 4; k++) {
            float *c = src->col[k], *d = dest->col[k];
            float r0 = c[0] * lparam->mul[0], r1 = c[1] * lparam->mul[1], r2 = c[2] * lparam->mul[2], w = c[3];
            d[0] = r0 + lparam->add[0];
            d[1] = r1 + lparam->add[1];
            d[2] = r2 + lparam->add[2];
            d[3] = w;
        }
        for (int j = 0; j < lparam->post; j++) {
            for (int k = 0; k < 4; k++) {
                float *d = dest->col[k];
                d[0] = d[0] * d[0];
                d[1] = d[1] * d[1];
                d[2] = d[2] * d[2];
            }
        }
    }
}

/* display list sizes: transparent (mflag 0x40 / 0x200) vs opaque models */
static int __AllocDisplyaList(void)
{
    int numTrans = 0, numOpacity = 0;
    struct {
        StModel *m;
        int n;
    } sets[4] = {{Map, STAGE_ModelNum[1]}, {Obj, STAGE_ModelNum[2]}, {Dobj, STAGE_ModelNum[8]}, {Chr, STAGE_ModelNum[3]}};
    for (int s = 0; s < 4; s++) {
        StModel *mp = sets[s].m;
        for (int i = 0; i < sets[s].n; i++, mp++) {
            if ((mp->mflag & 0x40) || (mp->mflag & 0x200)) numTrans++;
            else numOpacity++;
        }
    }
    return MkDL_Create(numTrans, numOpacity + 0x34, g_sortie.pconstant->isSplit ? 0x200000 : 0x100000);
}

static inline StModel *stage_alloc_models(int n)
{
    return n > 0 ? (StModel *)Memm_Alloc(1, n * 0x180, 0x10) : 0;
}

/* (allocation order is observable: heap addresses) */
static void __AllocStageMemory(ArrayFileSt *ap, ArrayElementMapSt *paeMap, ArrayElementObjSt *paeObj,
                               ArrayElementDobjSt *paeDobj, ArrayElementChrSt *paeChr)
{
    PS2_FRAME(0xf0);
    int num = paeMap ? paeMap->num : ap->head.mnum;
    int onum = paeObj ? paeObj->num : ap->head.onum;
    int dnum = paeDobj ? paeDobj->num : 0;
    int cnum = paeChr ? paeChr->num : ap->head.cnum;
    int fnum = DF_NPCTAB_GetChrNum();
    cnum += fnum;
    StgInfo.num_fchr = (unsigned short)fnum;
    StgInfo.cnum = (unsigned short)cnum;
    Map = stage_alloc_models(num);
    Obj = stage_alloc_models(onum);
    Chr = stage_alloc_models(cnum);
    Dobj = stage_alloc_models(dnum);
    MapS = stage_alloc_models(num);
    ObjS = stage_alloc_models(onum);
    ChrS = stage_alloc_models(cnum);
    DobjS = stage_alloc_models(dnum);
    for (int i = 0; i < 9; i++) STAGE_ModelNum[i] = 0;   /* (memset 0x24) */
    STAGE_ModelNum[1] = num;
    STAGE_ModelNum[2] = onum;
    STAGE_ModelNum[3] = cnum;
    STAGE_ModelNum[8] = dnum;
    STAGE_ModelNum[4] = NowPlayerNum;
    ZeroMemory32b(Map, (unsigned int)(num * 0x180) >> 2);
    ZeroMemory32b(MapS, (unsigned int)(num * 0x180) >> 2);
    ZeroMemory32b(Obj, (unsigned int)(onum * 0x180) >> 2);
    ZeroMemory32b(ObjS, (unsigned int)(onum * 0x180) >> 2);
    ZeroMemory32b(Chr, (unsigned int)(cnum * 0x180) >> 2);
    ZeroMemory32b(ChrS, (unsigned int)(cnum * 0x180) >> 2);
    ZeroMemory32b(Dobj, (unsigned int)(dnum * 0x180) >> 2);
    ZeroMemory32b(DobjS, (unsigned int)(dnum * 0x180) >> 2);
    for (int i = 0; i < num; i++) Map[i].mlist = paeMap ? &paeMap->list[i] : &ap->map_list[i];
    for (int i = 0; i < onum; i++) Obj[i].mlist = paeObj ? &paeObj->list[i] : &ap->obj_list[i];
    for (int i = 0; i < dnum; i++) Dobj[i].mlist = &paeDobj->list[i];
    if (fnum > 0) {
        Farray = (ArraySt *)Memm_Alloc(1, fnum * 0x80, 0x10);
        ZeroMemory32b(Farray, (unsigned int)(fnum * 0x80) >> 2);
    } else {
        Farray = 0;
    }
    StgInfo.light = (_map_light_st *)Memm_Alloc(1, 0x1000, 0x40);
    StgInfo.light2 = (_map_light_st *)Memm_Alloc(1, 0x1000, 0x40);
    StgInfo.fog = (_map_fog_st *)Memm_Alloc(1, 0x400, 0x10);
    StgInfo.fogparam = (_map_fogparam_st *)Memm_Alloc(1, 0x200, 0x10);
    StgInfo.reverb = (_sound_attr_st *)Memm_Alloc(1, 0x400, 0x10);
    StgInfo.temp = (float *)Memm_Alloc(1, 0x80, 0x10);
    StgInfo.pscreff = (StMapScrEffSt *)Memm_Alloc(1, 0x600, 0x10);
    CopyMemory128b(StgInfo.light, ap->light, 0x1000);
    CopyMemory128b(StgInfo.fog, ap->fog, 0x400);
    CopyMemory128b(StgInfo.fogparam, ap->fogparam, 0x200);
    CopyMemory128b(StgInfo.reverb, ap->sound, 0x400);
    CopyMemory128b(StgInfo.temp, ap->_temp, 0x80);
    CopyMemory128b(StgInfo.pscreff, ap->screff, 0x600);
    madd_light(StgInfo.light, ap->light, &StgInfo.lparam);
    ArraySt *marr = paeMap ? paeMap->arr : ap->map;
    int g = GetGimkCount(marr, num);
    ArraySt *oarr = paeObj ? paeObj->arr : ap->obj;
    g += GetGimkCount(oarr, onum);
    if (paeDobj) g += GetGimkCount(paeDobj->arr, dnum);
    char *buff = (char *)Memm_Alloc(1, g * 0x40 + 0x1000, 0x10);
    DF_GIMK_Alloc(buff, marr, Map, num, oarr, Obj, onum, paeDobj ? paeDobj->arr : 0, Dobj, dnum);
    ENEMY_New(cnum);
    DF_HIT_Alloc(num, onum + dnum);
}

/* copy the local-matrix pool of memory region 2 to region 3 (draw path) */
void DF_STAGE_PushLocalMatAll(void)
{
    PS2_FRAME(0x30);
    unsigned int dst = (unsigned int)Memm_GetInfo(3, 0);
    unsigned int src = (unsigned int)Memm_GetInfo(2, 0);
    DmaMemCopy(dst, src, (unsigned int)Memm_GetInfo(2, 2) >> 4);
}

void DF_SetOneModelAttribute(StModel *mp, ArraySt *aap)
{
    mp->array = aap;
    mp->mflag = aap->mflag;
    mp->groupid = aap->groupid;
    memcpy(mp->loc, aap->loc, 16);
    memcpy(mp->ang, aap->ang, 16);
    memcpy(mp->scale, aap->scale, 16);
    mp->scale[3] = 1.0f;
    mp->nowhp = aap->hp;
    if (mp->mflag & 0x10) mp->use |= 3;
    mp->vuparam.mulalpha = 1.0f;
    mp->blend2 = 0x68;
    mp->alpha2 = 0x40;
    if (mp->kind & 1) mp->tod->nowtodid = 0;   /* (tod unchecked) */
    if (aap->relation && (mp->type == 2 || mp->type == 8)) mp->use |= 0x4000;
}

/* object matrices: local, (parent chain), then world = matrix2 x local; rd copies the world matrix */
static inline void stage_object_matrices(StModel *m, int chain, int copy_rd)
{
    for (int k = 0; k < m->nobj; k++) DF_MakeObjectLocalMatrix(*m->op[k].wrmatp, &m->op[k]);
    if ((m->kind & 1) && chain)
        for (int k = 0; k < m->nobj; k++) DF_MakeParentChain(&m->op[k]);
    for (int k = 0; k < m->nobj; k++) {
        float (*M)[4] = *m->op[k].wrmatp;
        vu_mul_matrix(M, (const float (*)[4])m->matrix2, (const float (*)[4])M);
        if (copy_rd) sceVu0CopyMatrix(*m->op[k].rdmatp, M);
    }
}

static char *init_chr_model(StModel *mp, StModel *mps, ArraySt *aap, char *rp, int num)
{
    PS2_FRAME(0xe0);
    StModel *mtop = mp;
    ArraySt *atop = aap;
    StModel *ms = mps;
    for (int ii = 0; ii < num; ii++, mp++, ms++, aap++) {
        if (!aap->id) continue;
        if (mp->on == 0) {
            rp = __SetModel(mtop, atop, ii, num, aap->id, rp, 2, aap->mflag, 0);
            char *s7 = __SetModel(mtop, atop, ii, num, aap->id, rp, 5, 0, 0);
            rp = (char *)DF_ReadOMDPointer(s7, ms, 1);
            ms->on = (unsigned short)aap->id;
            ms->kind |= 0x8000;
            ms->op = MDL_AllocObject(mp->nobj);
            for (int jj = ii + 1; jj < num; jj++) {
                if (aap->id != atop[jj].id) continue;
                StModel *d = &ChrS[jj];   /* (the global, not mps) */
                rp = (char *)DF_ReadOMDPointer(s7, d, 1);
                d->on = (unsigned short)aap->id;
                d->kind |= 0x8000;
                d->op = MDL_AllocObject(mp->nobj);
            }
        }
        mp->aid = (unsigned short)ii;
        mp->type = 3;
        DF_SetOneModelAttribute(mp, aap);
        mp->mflag |= 0x800;
        DF_MakeModelLocalMatrix_Model(mp, mp->loc, mp->ang, mp->scale);
        stage_object_matrices(mp, 1, 0);
        mp->LocY = mp->loc[1];
        mp->cflag |= aap->cflag;
        Model_DeleteDiscernVertex(mp);
    }
    return rp;
}

/* objects / destructibles: optional animation, high and low-end models, hit model */
static char *stage_init_obj(StModel *mp, StModel *mps, ArraySt *aap, char *rp, int num, int type)
{
    StModel *mtop = mp;
    ArraySt *atop = aap;
    for (int ii = 0; ii < num; ii++, mp++, mps++, aap++) {
        mp->mps = mps;
        if (!aap->id) continue;
        if (mp->on == 0) {
            mp->on = (unsigned short)aap->id;
            unsigned int noanim = 0;
            char *animp = 0;
            if (*(int *)(rp + 8) != -1) {
                animp = rp;
                rp = Model_ReadAnim(mp, rp, 1);
            } else {
                noanim = 1;
            }
            rp = __SetModel(mtop, atop, ii, num, aap->id, rp, 0, aap->mflag, noanim);
            char *r = __SetModel(mtop, atop, ii, num, aap->id, rp, 1, aap->mflag, noanim);
            if (animp) __SetModel(mtop, atop, ii, num, aap->id, animp, 4, 0, 0);
            rp = __SetModel(mtop, atop, ii, num, aap->id, r, 3, 0, 0);
        }
        mp->aid = (unsigned short)ii;
        mp->type = (char)type;
        DF_SetOneModelAttribute(mp, aap);
        DF_MDL_SetDrawFlag(mp, mp->vu1mp, mp->omdtable);
        DF_MDL_SetDrawFlag(mp, mp->vu1mplw, mp->omdtablelw);
        DF_MakeModelLocalMatrix_Model(mp, mp->loc, mp->ang, mp->scale);
        stage_object_matrices(mp, 1, 1);
        StModel *s = mp->mps;
        s->on = mp->on;
        s->op = mp->op;
        sceVu0CopyMatrix(s->matrix2, mp->matrix2);
    }
    return rp;
}

static char *init_dobj_model(StModel *mp, StModel *mps, ArraySt *aap, char *rp, int num)
{
    PS2_FRAME(0xd0);
    if (!rp) return 0;
    return stage_init_obj(mp, mps, aap, rp, num, 8);
}

static char *init_obj_model(StModel *mp, StModel *mps, ArraySt *aap, char *rp, int num)
{
    PS2_FRAME(0xd0);
    return stage_init_obj(mp, mps, aap, rp, num, 2);
}

/* map models: no animation (mflagex bit 0 set), matrices per object without parent chain */
static char *init_map_model(StModel *mp, StModel *mps, ArraySt *aap, char *rp, int num)
{
    PS2_FRAME(0xd0);
    StModel *mtop = mp;
    ArraySt *atop = aap;
    for (int ii = 0; ii < num; ii++, mp++, mps++, aap++) {
        mp->mps = mps;
        if (!aap->id) continue;
        if (mp->on == 0) {
            mp->on = (unsigned short)aap->id;
            char *r = __SetModel(mtop, atop, ii, num, aap->id, rp, 0, aap->mflag, 0x10001);
            r = __SetModel(mtop, atop, ii, num, aap->id, r, 1, aap->mflag, 1);
            rp = __SetModel(mtop, atop, ii, num, aap->id, r, 3, 0, 1);
        }
        mp->aid = (unsigned short)ii;
        mp->type = 1;
        DF_SetOneModelAttribute(mp, aap);
        DF_MDL_SetDrawFlag(mp, mp->vu1mp, mp->omdtable);
        DF_MDL_SetDrawFlag(mp, mp->vu1mplw, mp->omdtablelw);
        DF_MakeModelLocalMatrix_Model(mp, mp->loc, mp->ang, mp->scale);
        for (int k = 0; k < mp->nobj; k++) {
            DF_MakeObjectLocalMatrix(*mp->op[k].wrmatp, &mp->op[k]);
            float (*M)[4] = *mp->op[k].wrmatp;
            vu_mul_matrix(M, (const float (*)[4])mp->matrix2, (const float (*)[4])M);
            sceVu0CopyMatrix(*mp->op[k].rdmatp, M);
        }
        StModel *s = mp->mps;
        s->on = mp->on;
        s->op = mp->op;
        sceVu0CopyMatrix(s->matrix2, mp->matrix2);
    }
    return rp;
}

/* (does not clear s_pEvDataBuf / the six event-buffer statics, StgInfo.light2 / pscreff or BLTtagBG) */
static void reset_stage_vars(void)
{
    PS2_FRAME(0x10);
    SysInfo = 0;
    Array = 0;
    Map = 0;
    Obj = 0;
    Chr = 0;
    Dobj = 0;
    MapS = 0;
    ObjS = 0;
    ChrS = 0;
    DobjS = 0;
    ScrTopP = 0;
    Farray = 0;
    ChrKickP = 0;
    ChrShadowKickP = 0;
    BLTtagMap = 0;
    BLTtagSFX = 0;
    memset(&CopyOfSysInfo, 0, 0x200);
    StgInfo.light = 0;
    StgInfo.fog = 0;
    StgInfo.fogparam = 0;
    StgInfo.reverb = 0;
    StgInfo.temp = 0;
    StgInfo.lparam.mul[0] = 1.0f;
    StgInfo.lparam.mul[1] = 1.0f;
    StgInfo.lparam.mul[2] = 1.0f;
    StgInfo.lparam.on = 0;
    StgInfo.lparam.pre = 0;
    StgInfo.lparam.post = 0;
    StgInfo.lparam.add[0] = 0.0f;
    StgInfo.lparam.add[1] = 0.0f;
    StgInfo.lparam.add[2] = 0.0f;
    memset(&s_spttex, 0, 0x28);
}

void STAGE_MAddLight(void)
{
    madd_light(StgInfo.light2, StgInfo.light, &StgInfo.lparam);
}

void Stage_End(void)
{
}

void Stage_Begin(void)
{
    PS2_FRAME(0x30);
    CopyMemory128b(&CopyOfSysInfo, SysInfo, 0x200);
    if (CopyOfSysInfo.lensflareon == 1) *(float *)((char *)&CopyOfSysInfo + 0x17c) = 1.0f;   /* lensflare alpha */
    DF_SetDBuffClrCol(&DBUFF, SysInfo->colBg);
    DF_SetDBuffClrCol(&DBUFF2, SysInfo->colBg);
    CUR_DRAW_CAM = 0;
    ScriptInit((unsigned char *)ScrTopP);
    MultiTex_Init();
    /* (only the two first ACs and the stage's own characters - not the friends - get the first map region) */
    MyAC[0].mlist = Map->mlist;
    MyAC[1].mlist = Map->mlist;
    StModel *c = Chr;
    for (int i = 0; i < Array->head.cnum; i++) c[i].mlist = Map->mlist;
    DF_VP_New(4);
    DF_EnvBallInit(&Envball);
    if (AcOpt.shadow) {
        DF_InitChrShadows();
        DF_InitACShadows();
        DF_SetShadowAngle(CopyOfSysInfo.shadow_angy, CopyOfSysInfo.shadow_angv);
        DF_SetShadowOff(CopyOfSysInfo.shadow_off);
    }
    STAGE_InitDmyLocData();
    STAGE_InitDummy(Array);
    CLOUD_SetDefAll((struct_87c875f8 *)Array->cloud_def);
    ZeroMemory8b(s_PfxIntp, 0x30);
    s_PfxIntp[0].pfx_id = -1;
    s_PfxIntp[1].pfx_id = -1;
    s_PfxIntp[2].pfx_id = -1;
    /* six stage ECM sources (team 6), registered even at noise level 0 */
    for (int i = 0; i < 6; i++)
        EcmField_RegisterEcm(1, 6, (short)i, (unsigned short)stage_u32(CopyOfSysInfo.noiselv[i]));
}

/* region pointers of one model list (fog / light / character light / reverb / screen effect by id) */
static inline void stage_fix_list(_map_model_list_st *e, int n)
{
    for (; n; n--, e++) {
        e->lp_fog = StgInfo.fog + e->fogid;
        e->lp_light = StgInfo.light2 + e->lightid;
        e->lp_chrlight = StgInfo.light2 + e->clightid;
        e->lp_reverb = StgInfo.reverb + e->sndid;
        e->pscreff = StgInfo.pscreff + e->screffid;
    }
}

/* returns the end of the stage buffer, or NULL (file errors, map too large) */
char *Stage_Load(unsigned int noMap, unsigned int sizeMaxMapFile)
{
    PS2_FRAME(0x41d0);
    PS2_STACK(short[0x2000], tblSss, 0x1a0);
    PS2_STACK(unsigned int *, plMap, 0x41b4);
    PS2_STACK(unsigned int *, plSfx, 0x41b0);
    PS2_STACK(unsigned int *, plBg, 0x41ac);
    PS2_STACK(int, sizeHd, 0x41bc);
    PS2_STACK(int, sizeBd, 0x41b8);
    PS2_STACK(char *, dobjP, 0x41c4);
    PS2_STACK(char *, chrP, 0x41c0);
    PS2_STACK(int, dobjSize, 0x41cc);
    PS2_STACK(int, chrSize, 0x41c8);
    dobjSize = 0;
    chrSize = 0;
    dobjP = 0;
    reset_stage_vars();
    chrP = 0;
    int id = 10000 + 100 * (int)noMap;
    int maxsize = (File_GetSize(id) + 0xf) & ~0xf;
    char *buf = (char *)Memm_GetSpecialReadBuffer(maxsize < 0x194000 ? 0x194000 : maxsize);
    if (!buf) return 0;

    /* map sound bank: common (chr situation) + event bank merged */
    StSortieConstant *pc = stage_pc();
    int idC = id + 0x40 + pc->situChr, idE = id + 0x50 + pc->situEv;
    char *buf2 = buf + 0x64000, *hd = buf + 0xc8000, *bd = buf + 0xcc000;
    sizeHd = 0;
    sizeBd = 0;
    memset(tblSss, 0xff, 0x4000);
    if (File_GetSize(idC) < 0x64001 && File_GetSize(idE) < 0x64001 && NL_ReadData(idC, buf, 0x64000, 0) &&
        NL_ReadData(idE, buf2, 0x64000, 0)) {
        MixHdBd_Mix(hd, &sizeHd, bd, &sizeBd, tblSss, buf, buf2);
        if (sizeHd < 0x2001 && sizeBd < 0x8c001) DF_SND_Setting_MAPSE(hd, sizeHd, bd, sizeBd);
    }

    if (!__ReadAndExtractDmapFile(&dobjP, &dobjSize, id + 0x10 + stage_pc()->situDobj)) return 0;
    if (!__ReadAndExtractDmapFile(&chrP, &chrSize, id + 0x20 + stage_pc()->situChr)) return 0;
    if (!NL_ReadData(id, buf, maxsize, 0)) return 0;
    int usize = FsLZSSGetUncompressSize((_FS_LZSS_FILE_HEADER_ST *)(buf + 0x20));
    char *dstBuf = (char *)Memm_Alloc(1, usize, 0x80);
    if (!((unsigned int)(usize + (dobjSize + chrSize)) < sizeMaxMapFile)) {
        /* the original formats "Map file size is too huge" (MAP / DOBJ / CHR MB) into a stack buffer it never shows */
        return 0;
    }
    FsLZSSUncompress((unsigned char *)dstBuf, (unsigned char *)buf + 0x20);
    Memm_ReleaseSpecialReadBuffer();
    int *hdr = (int *)dstBuf;
    int off, extra;
    if (hdr[5] > 0) {
        off = hdr[5];
        extra = off + 0xc0000 - usize;
    } else {
        off = hdr[4];
        extra = off + 0x80000 - usize;
    }
    if (extra > 0) Memm_Alloc(1, extra, 0x10);   /* (relies on landing right after dstBuf) */
    else extra = 0;

    ArrayFileSt *ap = (ArrayFileSt *)(dstBuf + 0x40);
    ArrayElementChrSt *chr = (ArrayElementChrSt *)chrP;
    ArrayElementDobjSt *dobj = (ArrayElementDobjSt *)dobjP;
    if (chr) {
        ap->head.cnum = chr->num;
        ap->head.acnum = chr->acnum;
        ap->head.mtnum = chr->mtnum;
    }
    if (dobj) ap->head.dnum = dobj->num;
    if (chr) ap->head.cnum = chr->num;

    /* event bundle (the situation byte is sign-extended here) */
    int evId = 100 * (int)stage_pc()->map + (signed char)stage_pc()->situEv + 0x2740;
    int ok = Cdvd_GetSize2048(evId) > 0 && NL_ReadData(evId, g_pbufRead, 0x100000, 0) ? 1 : 0;
    if (ok) {
        char *rb = g_pbufRead;
        int n = FsLZSSGetUncompressSize((_FS_LZSS_FILE_HEADER_ST *)(rb + 0x20));
        if (n <= 0) {
            ok = 0;
        } else {
            s_pEvDataBuf = Memm_Alloc(1, (n + 0xf) & ~0xf, 0x10);
            if (!s_pEvDataBuf) {
                ok = 0;
            } else {
                FsLZSSUncompress((unsigned char *)s_pEvDataBuf, (unsigned char *)rb + 0x20);
                ok = 1;
            }
        }
        if (ok) {
            __DistributeEventData();
            ok = 1;
        }
    }
    if (!ok) return 0;

    NowPlayerNum += ap->head.acnum;
    NowFrndACNum = DF_NPCTAB_GetACNum();
    NowPlayerNum = NowPlayerNum + NowFrndACNum;
    NowPlayerNum = NowPlayerNum + NowAIACNum;
    __AllocStageMemory(ap, 0, 0, dobj, chr);
    stage_fix_list(ap->map_list, ap->head.mnum);
    stage_fix_list(ap->obj_list, ap->head.onum);
    if (dobj) stage_fix_list(dobj->list, dobj->num);

    int mnum = ap->head.mnum, onum = ap->head.onum, dnum = ap->head.dnum, cnum = ap->head.cnum;
    NL_LoopNowLoading(1);
    init_map_model(Map, MapS, ap->map, dstBuf + ap->head.mofs, mnum);
    NL_LoopNowLoading(1);
    init_obj_model(Obj, ObjS, ap->obj, dstBuf + ap->head.oofs, onum);
    NL_LoopNowLoading(1);
    if (dobjP) init_dobj_model(Dobj, DobjS, ((ArrayElementDobjSt *)dobjP)->arr, dobjP + 0x12010, dnum);
    NL_LoopNowLoading(1);
    char *chrTex = init_chr_model(Chr, ChrS, ((ArrayElementChrSt *)chrP)->arr, chrP + 0x8220, cnum);
    NL_LoopNowLoading(1);
    chr = (ArrayElementChrSt *)chrP;
    ap->head.sysinfo.tbpChrOfs = chr->tbpChrOfs;   /* (chrP unchecked) */
    ap->head.sysinfo.cbpChrOfs = chr->cbpChrOfs;
    for (int k = 0; k < 10; k++) ap->head.sysinfo.sndtop[k] += (unsigned int)(uintptr_t)dstBuf;
    CopyMemory(ap->stable, tblSss, 0x4000);
    PolygInit();
    PolygRegist(s_pPpgBuf);

    /* special transfer textures: the first marker of type 0xc whose cdmy[0] is jj + 1 */
    for (int jj = 0; jj < 4; jj++) {
        if (!(hdr[6 + jj] > 0)) continue;
        int i = 0;
        for (; i < 0x80; i++) {
            if (ap->marker[i].type == 0xc && ap->marker[i].cdmy[0] == jj + 1) {
                s_spttex.idxMarker[jj] = i;
                break;
            }
        }
        if (i == 0x80) continue;
        s_spttex.ptmxTrans[jj] = dstBuf + hdr[6 + jj];
        s_spttex.isEnable = 1;
    }

    /* textures: GS upload + readback and the three image-load packet lists (phase 2: renderer registration) */
    if (hdr[5] > 0) CopyMemory128b(g_pbufRead, dstBuf + hdr[5], hdr[3]);
    TmxTopP = dstBuf + off;
    BgTexP = TmxTopP + (hdr[5] > 0 ? 0x80000 : 0);
    DF_ReadTMXPointer(dstBuf + hdr[4], 1);
    sceGsSyncPath(0, 0);
    if (chrTex && *(int *)chrTex) {
        DF_ReadTMXPointer(chrTex, 1);
        sceGsSyncPath(0, 0);
    }
    DF_StoreImage(TmxTopP, 0x2e40, 0, 0, 0, 0x40, 0x400);
    DF_StoreImage(TmxTopP + 0x40000, 0x3240, 0, 0, 0, 0x40, 0x400);
    if (hdr[5] > 0) {
        sceGsSyncPath(0, 0);
        DF_ReadTMXPointer(g_pbufRead, 1);
        sceGsSyncPath(0, 0);
        DF_StoreImage(BgTexP, 0x2e40, 0, 0, 0, 0x40, 0x400);
    }
    char *img = TmxTopP;
    plMap = (unsigned int *)Memm_Alloc(1, 0x190, 0x10);
    unsigned int *l = plMap;
    for (int k = 0, tbp = 0x2e40; k < 2; k++, tbp += 0x400, img += 0x40000) GL_LoadImage(&plMap, img, tbp, 0x40, 0x400);
    GL_EndList(&plMap);
    BLTtagMap = l;
    img = MagicTexP;
    plSfx = (unsigned int *)Memm_Alloc(1, 0x190, 0x10);
    l = plSfx;
    for (int k = 0, tbp = 0x2e40; k < 2; k++, tbp += 0x400, img += 0x40000) GL_LoadImage(&plSfx, img, tbp, 0x40, 0x400);
    GL_EndList(&plSfx);
    BLTtagSFX = l;
    if (hdr[5] > 0) {
        img = BgTexP;
        plBg = (unsigned int *)Memm_Alloc(1, 0xd0, 0x10);
        l = plBg;
        GL_LoadImage(&plBg, img, 0x2e40, 0x40, 0x400);
        GL_EndList(&plBg);
        BLTtagBG = l;
    } else {
        BLTtagBG = 0;
    }

    char *end = dstBuf + usize + extra;
    NL_ReadData(6, Envballbuf, -1, 0);
    DF_ReadTP2Pointer(Envballbuf, &Envball, 0);
    OutputOMD_EnvBall(&Envball, (char *)Memm_Alloc(1, 0x5800, 0x10));

    /* connected-group counts: layer[] up to the first 0xff (at most 0x3f), stored in layer[0x3f] */
    for (unsigned char g = 0; g < ap->head.gnum; g++) {
        unsigned char *ly = ap->group[g].layer;
        int t = 0;
        while (ly[t] != 0xff) {
            t++;
            if (!(t < 0x3f)) break;
        }
        ly[0x3f] = (unsigned char)t;
    }
    if (s_pMesBuf) memcpy(&ap->message, s_pMesBuf, 0x2800);
    SysInfo = &ap->head.sysinfo;
    Array = ap;
    __AllocDisplyaList();
    s_paeDobj = (ArrayElementDobjSt *)dobjP;
    s_paeChr = (ArrayElementChrSt *)chrP;
    return end;
}

/* read and decompress a dmap file through the shared read buffer (*psize is written before it is checked) */
static int __ReadAndExtractDmapFile(char **ppbuf, int *psize, int cdvdid)
{
    PS2_FRAME(0x50);
    char *buf = g_pbufRead;
    int n = File_GetSize(cdvdid);
    if (n <= 0) return 0;
    if (!(n < 0x100001)) return 0;
    if (!NL_ReadData(cdvdid, buf, 0x100000, 0)) return 0;
    *psize = FsLZSSGetUncompressSize((_FS_LZSS_FILE_HEADER_ST *)(buf + 0x20));
    if (*psize <= 0) return 0;
    char *p = (char *)Memm_Alloc(1, (*psize + 0xf) & ~0xf, 0x10);
    FsLZSSUncompress((unsigned char *)p, (unsigned char *)buf + 0x20);
    *ppbuf = p;
    return 1;
}

static inline void stage_prim_change(StModel *pm, unsigned int *v, OmdTableSt *t, unsigned int mflag)
{
    int n = pm->nmobj;
    if (mflag & 0x10002) {
        if (mflag & 0x40) DF_VU1ModelPrimChange(t, n, 0x7e0, 0x40);
        DF_VU1ModelCallFChange(v, t, n, 0);
    } else if (mflag & 0x40) {
        DF_VU1ModelPrimChange(t, n, 0x7e0, 0x60);
    } else {
        DF_VU1ModelPrimChange(t, n, 0x7e0, 0x20);
    }
}

/* bind the data at rp (kind 0 OMD, 1 low-end OMD, 2 character OMD, 3 HMD, 4 / 5 animation) to model `begin`, then
 * to every later model with the same id (sharing the same data); returns the advanced data pointer of the first */
static char *__SetModel(StModel *mtop, ArraySt *atop, int begin, int num, unsigned int id, char *rp, int kind,
                        unsigned int mflag, unsigned int mflagex)
{
    PS2_FRAME(0x110);
    StModel *pm = mtop + begin;
    ArraySt *pa = atop + begin;
    unsigned int anim = (mflagex & 1) == 0;
    char *ret = 0;
    switch ((unsigned int)kind) {
    case 0:
        ret = (char *)DF_ReadOMDPointer(rp, pm, anim);
        stage_prim_change(pm, pm->vu1mp, pm->omdtable, mflag);
        break;
    case 1:
        ret = (char *)DF_ReadOMDPointerLowEnd(rp, pm);
        stage_prim_change(pm, pm->vu1mplw, pm->omdtablelw, mflag);
        break;
    case 2:
        ret = (char *)DF_ReadOMDPointer(rp, pm, anim);
        pm->on = (unsigned short)id;
        pm->kind |= 0x8000;
        DF_VU1ModelPrimChange(pm->omdtable, pm->nmobj, 0x7e0, 0x70);
        break;
    case 3: ret = (char *)DF_ReadHMDPointer(rp, pm); break;
    case 4: ret = Model_ReadAnim(pm, rp, 0); break;
    case 5: ret = Model_ReadAnim(pm, rp, 1); break;
    }
    for (int i = begin + 1; i < num; i++) {
        pm++;
        pa++;
        if (pa->id != id) continue;
        switch ((unsigned int)kind) {
        case 0: DF_ReadOMDPointer(rp, pm, anim); break;
        case 1: DF_ReadOMDPointerLowEnd(rp, pm); break;
        case 2:
            DF_ReadOMDPointer(rp, pm, anim);
            pm->kind |= 0x8000;
            break;
        case 3: pm->hmd = (HmdHeadSt *)rp; break;
        case 4:
        case 5: Model_ReadAnim(pm, rp, 0); break;
        }
        pm->on = (unsigned short)id;
    }
    return ret;
}

#ifndef ACNB_SOURCEPORT
/* 9 int arguments: mflagex is the caller's stack word sp+0 (gen_bridge cannot wrap it). Interpreted callers reach the
 * native through this wrapper. */
static void bw____SetModel(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    char *r = __SetModel((StModel *)bridge_ptr(c->gpr[A0].uw[0]), (ArraySt *)bridge_ptr(c->gpr[A1].uw[0]),
                         c->gpr[A2].sw[0], c->gpr[A3].sw[0], c->gpr[T0].uw[0], (char *)bridge_ptr(c->gpr[T1].uw[0]),
                         c->gpr[T2].sw[0], c->gpr[T3].uw[0], rd32(sp));
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif
