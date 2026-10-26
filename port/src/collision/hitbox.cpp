/* src/collision/hitbox.cpp - uniform grid of 160-unit boxes over the stage for the character, bullet and lock-on
 * tests (whole file). s_HitBoxInfo: grid origin mapMin and box count per axis; s_pHitBoxHead = the boxes, x-major.
 * A box lists the hit models whose bounding box starts in it (HitBoxListSt; StModel.pBoxList); a model that spills
 * over a box's maximum side is listed in the neighbours on the + sides too (pNearBox: +x, +y, +z, +xy, +yz, +zx,
 * +xyz), so a test looks at the box of its own minimum corner plus the neighbours its box spills into
 * (__GetCollFlag bits, s_CollFlag[i] = bit of pNearBox[i]). hit_atr = what the box's models have (1 floor, 2 wall,
 * 4 water). Moving models (mflag bit 0) keep their entries in a HitBoxAbsHitListSt (s_pHitBoxUpdateList) and are
 * re-sorted every frame. A test visits each model once (s_MarkMdl). Shapes and results live in the scratchpad
 * work area (hitwk.h). */
#include "acnb/sdk.h"
#include "hitwk.h"

#ifdef ACNB_SOURCEPORT
unsigned int s_CollFlag[7] = {1, 2, 4, 8, 0x10, 0x20, 0x40};
#endif

static const float BOX = 160.0f;
static const float FMAX = ps2_u2f(0x7f7fffff);

static int __IsPrimInOut(HmdPrimSt *pVert, int VertNum) { return hit_prim_in_box(pVert, VertNum); }

/* which + side neighbours of the box the point max lies beyond: bit 0 x, 1 y, 2 z, 3 xy, 4 yz, 5 zx, 6 xyz */
static inline unsigned int coll_flag(const float *max, const HitBoxBoxSt *b)
{
    bool x = !(max[0] <= b->Max[0]), y = !(max[1] <= b->Max[1]), z = !(max[2] <= b->Max[2]);
    unsigned int f = 0;
    if (x) f |= 1;
    if (y) f |= 2;
    if (z) f |= 4;
    if (x && y) f |= 8;
    if (y && z) f |= 0x10;
    if (z && x) f |= 0x20;
    if (x && y && z) f |= 0x40;
    return f;
}

/* for the tested shape's box */
static unsigned int __GetCollFlag(HitBoxBoxSt *pBox) { return pBox ? coll_flag(HIT_WK->max, pBox) : 0; }

static int __IsInRegionModel(StModel *pMdl, int mdlFlag, int useFlag)
{
    const HitSprWk *w = HIT_WK;
    const _map_model_list_st *ml = pMdl->mlist;
    float r = ml->R;
    if (pMdl->mflag & mdlFlag) return 0;
    if (pMdl->use & useFlag) return 0;
    if (!(w->min[1] <= pMdl->Ybottom)) return 0;
    if (w->max[1] < pMdl->Ytop) return 0;
    if (!(w->min[0] <= ml->sphere[0] + r)) return 0;
    if (!(w->min[2] <= ml->sphere[2] + r)) return 0;
    if (w->max[0] < ml->sphere[0] - r) return 0;
    return !(w->max[2] < ml->sphere[2] - r);
}

/* appends m to arr[0..*n) unless it is there */
static inline void add_unique(StModel *m, int *n, StModel **arr)
{
    int i = *n - 1;
    for (; i >= 0; i--)
        if (arr[i] == m) break;
    if (i < 0) arr[(*n)++] = m;
}

/* the box's models, sorted into those overlapping the shape's box and the others (array sizes not checked) */
static void __GetInOutRegionBox(HitBoxBoxSt *pBox, int mdlFlag, int useFlag, int *pInMdlNum, StModel **pInArrMdl,
                                int numInArrMax, int *pOutMdlNum, StModel **pOutArrMdl, int numOutArrMax)
{
    if (!pBox) return;
    for (HitBoxListSt *l = pBox->pList; l;) {
        StModel *m = l->pMdl;
        l = l->pNext;
        if (__IsInRegionModel(m, mdlFlag, useFlag)) add_unique(m, pInMdlNum, pInArrMdl);
        else add_unique(m, pOutMdlNum, pOutArrMdl);
    }
}

static void __GetInRegionBox(HitBoxBoxSt *pBox, int mdlFlag, int useFlag, int *pMdlNum, StModel **pArrMdl,
                             int numArrMax)
{
    if (!pBox) return;
    for (HitBoxListSt *l = pBox->pList; l;) {
        StModel *m = l->pMdl;
        l = l->pNext;
        if (!__IsInRegionModel(m, mdlFlag, useFlag)) continue;
        if (!(*pMdlNum < numArrMax)) return;
        add_unique(m, pMdlNum, pArrMdl);
    }
}

/* first visit of the model in this test (remembers up to 16) */
static inline bool mark_model(StModel *m)
{
    for (int i = 0; i < s_NowMarkIndex; i++)
        if (s_MarkMdl[i] == m) return false;
    if (s_NowMarkIndex < 16) {
        s_MarkMdl[s_NowMarkIndex] = m;
        s_NowMarkIndex++;
    }
    return true;
}

/* the model's volume (y: Ytop..Ybottom, x / z: sphere +- R) overlaps the shape's box */
static inline bool mdl_on_box(const StModel *m)
{
    const HitSprWk *w = HIT_WK;
    if (w->max[1] < m->Ytop || !(w->min[1] <= m->Ybottom)) return false;
    const _map_model_list_st *ml = m->mlist;
    float r = ml->R;
    if (w->max[0] < ml->sphere[0] - r || w->max[2] < ml->sphere[2] - r) return false;
    return w->min[0] <= ml->sphere[0] + r && w->min[2] <= ml->sphere[2] + r;
}

/* floor / water: a new hit counts if it is the first or nearer to the cylinder's middle height than the kept one */
static inline bool nearer_y(int isHit)
{
    const HitSprWk *w = HIT_WK;
    if (!isHit) return true;
    float a = fabsf(w->cyl.Center[1] - w->info.hitpos[1]);
    float b = fabsf(w->cyl.Center[1] - w->hold[0]);
    return a < b;
}

/* floor-type hit of the Y cylinder against the strips of the box's models that pass the filter */
template <class Accept>
static void cyl_floor_hits(StModel *m, int *pIsHit, Accept accept)
{
    HitSprWk *w = HIT_WK;
    hit_mesh_to_spr(m);
    struct_de4d68f5 *ph = m->hmd->php;
    for (unsigned int np = m->hmd->p_num;; ph = prim_next(ph)) {
        HmdPrimSt *v = prim_verts(ph);
        if (accept(v) && __IsPrimInOut(v, ph->pv_num)) {
            for (int t = ph->pv_num - 2;; v++) {
                if (HitFuncPolygonBBCheck(*v[0].v_id, *v[1].v_id, *v[2].v_id, w->min, w->max)) {
                    HitFuncCylinderPolygonYukaSPR(*v[0].v_id, *v[1].v_id, *v[2].v_id);
                    if (w->info.hit && nearer_y(*pIsHit)) {
                        *pIsHit = 1;
                        w->info.color_B = (unsigned char)v->b;
                        w->hold[0] = w->info.hitpos[1];
                        HitFuncSetHitMdl(m);
                    }
                }
                if (--t == 0) break;
            }
        }
        if (--np == 0) break;
    }
}

/* water surfaces (polygons with b = 1, g = 0xff) */
static void __HitProcWater(HitBoxBoxSt *pBox, int *pIsHit)
{
    PS2_FRAME(0xc0);
    if (!pBox || !(pBox->hit_atr & 4)) return;
    for (HitBoxListSt *l = pBox->pList; l;) {
        StModel *m = l->pMdl;
        HmdHeadSt *h = m->hmd;
        l = l->pNext;
        if (!h->water || (m->use & 2) || h->v_num < 4 || !mdl_on_box(m) || !mark_model(m)) continue;
        cyl_floor_hits(m, pIsHit, [](const HmdPrimSt *v) { return v->b == 1 && v->g == 0xff; });
    }
}

/* walls (polygons with r = 0xff, not water / no-hit): each hit pushes the cylinder out of the wall in x / z */
static void __HitProcKabe(HitBoxBoxSt *pBox, int *pIsHit)
{
    PS2_FRAME(0xb0);
    HitSprWk *w = HIT_WK;
    if (!pBox || !(pBox->hit_atr & 2)) return;
    for (HitBoxListSt *l = pBox->pList; l;) {
        StModel *m = l->pMdl;
        HmdHeadSt *h = m->hmd;
        l = l->pNext;
        if (!h->kabe || (m->use & 2) || (m->array->mflag & 0x40000) || h->v_num < 4 || !mdl_on_box(m)) continue;
        if (!mark_model(m)) continue;
        hit_mesh_to_spr(m);
        struct_de4d68f5 *ph = h->php;
        for (unsigned int np = h->p_num;; ph = prim_next(ph)) {
            HmdPrimSt *v = prim_verts(ph);
            if (v->r == 0xff && v->b != 1 && v->b != 7 && __IsPrimInOut(v, ph->pv_num)) {
                for (int t = ph->pv_num - 2;; v++) {
                    if (HitFuncPolygonBBCheck(*v[0].v_id, *v[1].v_id, *v[2].v_id, w->min, w->max)) {
                        HitFuncCylinderPolygonKabeSPR(*v[0].v_id, *v[1].v_id, *v[2].v_id);
                        if (w->info.hit) {
                            float d[3];
                            for (int i = 0; i < 3; i++) d[i] = w->cyl.P[i] - w->info.hitpos[i];
                            float len = ps2_sqrt(d[0] * d[0] + d[2] * d[2]);
                            float pen = w->cyl.R - len;
                            if (!(pen < 0.0f)) {
                                *pIsHit = 1;
                                float s = ps2_div(pen, len);
                                for (int i = 0; i < 3; i++) w->cyl.P[i] = w->cyl.P[i] + d[i] * s;
                                w->info.color_B = (unsigned char)v->b;
                                HitFuncSetHitMdl(m);
                            }
                        }
                    }
                    if (--t == 0) break;
                }
            }
            if (--np == 0) break;
        }
    }
}

/* floors (polygons with g = 0xff, not water / no-hit) */
static void __HitProcYuka(HitBoxBoxSt *pBox, int *pIsHit)
{
    PS2_FRAME(0xc0);
    if (!pBox || !(pBox->hit_atr & 1)) return;
    for (HitBoxListSt *l = pBox->pList; l;) {
        StModel *m = l->pMdl;
        HmdHeadSt *h = m->hmd;
        l = l->pNext;
        if (!h->yuka || (m->use & 2) || (m->array->mflag & 0x40000) || h->v_num < 4 || !mdl_on_box(m)) continue;
        if (!mark_model(m)) continue;
        cyl_floor_hits(m, pIsHit, [](const HmdPrimSt *v) { return v->g == 0xff && v->b != 1 && v->b != 7; });
    }
}

/* models a sphere / line may hit: isAtc skips array mflag 0x80000, isChr skips 0x40000 */
static inline bool shot_model(StModel *m, int isAtc, int isChr)
{
    if (m->use & 2) return false;
    if (isAtc && (m->array->mflag & 0x80000)) return false;
    if (isChr && (m->array->mflag & 0x40000)) return false;
    return m->hmd->v_num >= 4 && mdl_on_box(m);
}

/* keeps the hit nearest to the sphere's centre (info.dist2) */
static void __HitProcSphere(HitBoxBoxSt *pBox, int *pIsHit, int isAtc, int isChr)
{
    PS2_FRAME(0xd0);
    HitSprWk *w = HIT_WK;
    if (!pBox) return;
    for (HitBoxListSt *l = pBox->pList; l;) {
        StModel *m = l->pMdl;
        l = l->pNext;
        if (!shot_model(m, isAtc, isChr) || !mark_model(m)) continue;
        hit_mesh_to_spr(m);
        struct_de4d68f5 *ph = m->hmd->php;
        for (unsigned int np = m->hmd->p_num;; ph = prim_next(ph)) {
            HmdPrimSt *v = prim_verts(ph);
            if (v->b != 7 && __IsPrimInOut(v, ph->pv_num)) {
                for (int t = ph->pv_num - 2;; v++) {
                    if (HitFuncPolygonBBCheck(*v[0].v_id, *v[1].v_id, *v[2].v_id, w->min, w->max)) {
                        HitFuncSpherePolygonSPR(*v[0].v_id, *v[1].v_id, *v[2].v_id);
                        if (w->info.hit) {
                            float d[3];
                            for (int i = 0; i < 3; i++) d[i] = w->sphere.P[i] - w->info.hitpos[i];
                            float x = d[0] * d[0], y = d[1] * d[1], z = d[2] * d[2];
                            float dist2 = z + (x + y);
                            if (!*pIsHit || dist2 < w->info.dist2) {
                                *pIsHit = 1;
                                w->info.color_B = (unsigned char)v->b;
                                w->info.dist2 = dist2;
                                memcpy(w->hold, w->info.hitpos, 16);
                                HitFuncSetHitMdl(m);
                            }
                        }
                    }
                    if (--t == 0) break;
                }
            }
            if (--np == 0) break;
        }
    }
}

/* every hit shortens the line, so the last one is the nearest */
static void __HitProcLine(HitBoxBoxSt *pBox, int *pIsHit, int isAtc, int isChr)
{
    PS2_FRAME(0xf0);
    HitSprWk *w = HIT_WK;
    if (!pBox) return;
    for (HitBoxListSt *l = pBox->pList; l;) {
        StModel *m = l->pMdl;
        l = l->pNext;
        if (!shot_model(m, isAtc, isChr) || !mark_model(m)) continue;
        hit_mesh_to_spr(m);
        struct_de4d68f5 *ph = m->hmd->php;
        for (unsigned int np = m->hmd->p_num;; ph = prim_next(ph)) {
            HmdPrimSt *v = prim_verts(ph);
            if (v->b != 7 && __IsPrimInOut(v, ph->pv_num)) {
                for (int t = ph->pv_num - 2, k = 0;; v++, k++) {
                    HitFuncLinePolygonSPR(*v[0].v_id, *v[1].v_id, *v[2].v_id);
                    if (w->info.hit) {
                        *pIsHit = 1;
                        float tt = w->info.tt1;
                        for (int i = 0; i < 3; i++) w->line.V[i] = w->line.V[i] * tt;
                        for (int i = 0; i < 3; i++) w->hold[i] = w->line.P[i] + w->line.V[i];
                        w->hold[3] = w->line.P[3];
                        w->line.L = w->line.L * tt;
                        w->info.color_B = (unsigned char)v->b;
                        HitFuncSetHitMdl(m);
                        hit_line_box();
                        hit_line_normal(ph, k);
                    }
                    if (--t == 0) break;
                }
            }
            if (--np == 0) break;
        }
    }
}

/* index of the box containing pos on the axis (binary search; clamped by the caller) */
static int __NearAxisIndex(float pos, int axis)
{
    int hi = s_HitBoxInfo.num_axis[axis] - 1, lo = 0;
    if (0 < hi) {
        do {
            int mid = (hi + lo) >> 1;
            float b = s_HitBoxInfo.mapMin[axis] + BOX * (float)mid;
            if (pos < b) {
                hi = mid - 1;
            } else {
                lo = mid + 1;
                if (pos <= b + BOX) return mid;
            }
        } while (lo < hi);
    }
    return (hi + lo) >> 1;
}

static HitBoxBoxSt *__GetBoxPointerForXYZ(int x, int y, int z)
{
    const int *n = s_HitBoxInfo.num_axis;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (z < 0) z = 0;
    if (n[0] - 1 < x) x = n[0] - 1;
    if (n[1] - 1 < y) y = n[1] - 1;
    if (n[2] - 1 < z) z = n[2] - 1;
    return s_pHitBoxHead + (x * (n[1] * n[2]) + y * n[2] + z);
}

/* the box holding the minimum corner of the model bounding box from HitSpcGetHitModelBoundBox */
static HitBoxBoxSt *bb_box(void)
{
    const HitSprWk *w = HIT_WK;
    int x = __NearAxisIndex(w->bb.min[0], 0);
    int y = __NearAxisIndex(w->bb.min[1], 1);
    int z = __NearAxisIndex(w->bb.min[2], 2);
    return __GetBoxPointerForXYZ(x, y, z);
}

/* the box holding the minimum corner of the tested shape's box */
static HitBoxBoxSt *shape_box(void)
{
    const HitSprWk *w = HIT_WK;
    int x = __NearAxisIndex(w->min[0], 0);
    int y = __NearAxisIndex(w->min[1], 1);
    int z = __NearAxisIndex(w->min[2], 2);
    return __GetBoxPointerForXYZ(x, y, z);
}

/* a moving model: entries in a HitBoxAbsHitListSt appended to s_pHitBoxUpdateList */
static void __InsertUpdateMdl(StModel *pMdl)
{
    HitBoxBoxSt *box = bb_box();
    HitBoxAbsHitListSt *e;
    if (!s_pHitBoxUpdateList) {
        s_pHitBoxUpdateList = (HitBoxAbsHitListSt *)Memm_Alloc(1, 0x90, 0x10);
        e = s_pHitBoxUpdateList;
    } else {
        HitBoxAbsHitListSt *p = s_pHitBoxUpdateList;
        while (p->pNext) p = p->pNext;
        p->pNext = (HitBoxAbsHitListSt *)Memm_Alloc(1, 0x90, 0x10);
        e = p->pNext;
    }
    e->pMdl = pMdl;
    e->pBox = box;
    e->pNext = 0;
    __AddABSModel(pMdl, box, e);
}

static void __SetOneBoxAtr(HitBoxBoxSt *pBox)
{
    unsigned int atr = 0;
    for (HitBoxListSt *l = pBox->pList; l; l = l->pNext) {
        HmdHeadSt *h = l->pMdl->hmd;
        if (h->yuka) atr |= 1;
        if (h->kabe) atr |= 2;
        if (h->water) atr |= 4;
    }
    pBox->hit_atr = atr;
}

/* the static model nearest to the box centre */
static void __SetOneBoxParentMdl(HitBoxBoxSt *pBox)
{
    float best = FMAX, c[3];
    for (int i = 0; i < 3; i++) c[i] = (pBox->Max[i] + pBox->Min[i]) * 0.5f;
    int n = HitMapNum + HitObjNum;
    for (int i = 0; i < n; i++) {
        StModel *m = hit_model(i);
        if (m->mflag & 1) continue;
        const float *s = m->mlist->sphere;
        float dx = s[0] - c[0], dy = s[1] - c[1], dz = s[2] - c[2];
        float d = dz * dz + (dx * dx + dy * dy);
        if (d < best) {
            pBox->pParentMdl = m;
            best = d;
        }
    }
}

static int __IsEntryMdl(HitBoxBoxSt *pBox, StModel *pMdl)
{
    for (HitBoxListSt *l = pBox->pList; l; l = l->pNext)
        if (l->pMdl == pMdl) return 1;
    return 0;
}

static void __RemoveMdl(HitBoxBoxSt *pBox, StModel *pMdl)
{
    HitBoxListSt *l = pBox->pList;
    while (l && l->pMdl != pMdl) l = l->pNext;
    if (!l) return;
    HitBoxListSt *back = l->pBack, *next = l->pNext;
    if (!back) pBox->pList = next;
    else back->pNext = next;
    if (next) next->pBack = back;
    pBox->mdl_num--;
}

/* appends the model's current entry (pMdl->pBoxList) to the box */
static void __AddMdl(HitBoxBoxSt *pBox, StModel *pMdl)
{
    pBox->mdl_num++;
    HitBoxListSt *l;
    if (!pBox->pList) {
        pBox->pList = pMdl->pBoxList;
        l = pBox->pList;
        l->pBack = 0;
    } else {
        HitBoxListSt *p = pBox->pList;
        while (p->pNext) p = p->pNext;
        p->pNext = pMdl->pBoxList;
        l = p->pNext;
        l->pBack = p;
    }
    l->pNext = 0;
}

/* appends a new entry for the model to the box (its pBox is left unset) */
static HitBoxListSt *__InsertMdl(HitBoxBoxSt *pBox, StModel *pMdl)
{
    pBox->mdl_num++;
    HitBoxListSt *l;
    if (!pBox->pList) {
        l = (HitBoxListSt *)Memm_Alloc(1, 0x10, 0x10);
        l->pMdl = pMdl;
        l->pBack = 0;
        l->pNext = 0;
        pBox->pList = l;
        return l;
    }
    HitBoxListSt *p = pBox->pList;
    while (p->pNext) p = p->pNext;
    p->pNext = (HitBoxListSt *)Memm_Alloc(1, 0x10, 0x10);
    l = p->pNext;
    l->pMdl = pMdl;
    l->pBack = p;
    l->pNext = 0;
    return l;
}

/* for the model bounding box */
static unsigned int __GetMdlCollFlag(HitBoxBoxSt *pBox) { return coll_flag(HIT_WK->bb.max, pBox); }

/* (re)lists a moving model: its box, and each neighbour its bounding box spills into (one level only) */
static void __AddABSModel(StModel *pMdl, HitBoxBoxSt *pBox, HitBoxAbsHitListSt *pABSList)
{
    HitBoxListSt *e = &pABSList->List;
    memset(e, 0, 0x10);
    e->pMdl = pMdl;
    e->pBox = pBox;
    pMdl->pBoxList = e;
    __AddMdl(pBox, pMdl);
    for (int i = 0; i < 7; i++) {
        if (!(s_CollFlag[i] & 0x7f)) continue;
        HitBoxBoxSt *nb = pBox->pNearBox[i];
        if (!nb || __IsEntryMdl(nb, pMdl)) continue;
        e = &pABSList->NearList[i];
        memset(e, 0, 0x10);
        e->pMdl = pMdl;
        e->pBox = nb;
        pMdl->pBoxList = e;
        __AddMdl(nb, pMdl);
    }
}

/* lists a static model in the neighbours its bounding box spills into, recursively */
static void __EntryNearMdl(HitBoxBoxSt *pBox, StModel *pMdl)
{
    unsigned int f = __GetMdlCollFlag(pBox);
    if (!f) return;
    for (int i = 0; i < 7; i++) {
        if (!(f & s_CollFlag[i])) continue;
        HitBoxBoxSt *nb = pBox->pNearBox[i];
        if (!nb || __IsEntryMdl(nb, pMdl)) continue;
        __InsertMdl(nb, pMdl);
        __EntryNearMdl(nb, pMdl);
    }
}

static void __EntryOneMdl(StModel *pMdl)
{
    HitBoxBoxSt *box = bb_box();
    pMdl->pBoxList = __InsertMdl(box, pMdl);
    pMdl->pBoxList->pBox = box;
    __EntryNearMdl(box, pMdl);
}

/* neighbours inside the grid (sic: the +xyz one only checks y and z, x clamps at the edge) */
static void __SetNearBox(HitBoxBoxSt *pBox, int x, int y, int z)
{
    bool nx = x < s_HitBoxInfo.num_axis[0] - 1, ny = y < s_HitBoxInfo.num_axis[1] - 1,
         nz = z < s_HitBoxInfo.num_axis[2] - 1;
    for (int i = 0; i < 7; i++) pBox->pNearBox[i] = 0;
    if (nx) pBox->pNearBox[0] = __GetBoxPointerForXYZ(x + 1, y, z);
    if (ny) pBox->pNearBox[1] = __GetBoxPointerForXYZ(x, y + 1, z);
    if (nz) pBox->pNearBox[2] = __GetBoxPointerForXYZ(x, y, z + 1);
    if (nx && ny) pBox->pNearBox[3] = __GetBoxPointerForXYZ(x + 1, y + 1, z);
    if (ny && nz) pBox->pNearBox[4] = __GetBoxPointerForXYZ(x, y + 1, z + 1);
    if (nz && nx) pBox->pNearBox[5] = __GetBoxPointerForXYZ(x + 1, y, z + 1);
    if (ny && nz) pBox->pNearBox[6] = __GetBoxPointerForXYZ(x + 1, y + 1, z + 1);
}

static void __SetBoxBB(HitBoxBoxSt *pBox, int x, int y, int z)
{
    pBox->Min[0] = s_HitBoxInfo.mapMin[0] + BOX * (float)x;
    pBox->Min[1] = s_HitBoxInfo.mapMin[1] + BOX * (float)y;
    pBox->Min[2] = s_HitBoxInfo.mapMin[2] + BOX * (float)z;
    pBox->Min[3] = 1.0f;
    for (int i = 0; i < 3; i++) pBox->Max[i] = BOX + pBox->Min[i];
    pBox->Max[3] = 1.0f;
}

static void __MakeBox(void)
{
    int size = s_HitBoxInfo.num_box * (int)sizeof(HitBoxBoxSt);
    s_pHitBoxHead = (HitBoxBoxSt *)Memm_Alloc(1, size, 0x40);
    ZeroMemory32b(s_pHitBoxHead, size / 4);
    for (int x = 0; x < s_HitBoxInfo.num_axis[0]; x++)
        for (int y = 0; y < s_HitBoxInfo.num_axis[1]; y++)
            for (int z = 0; z < s_HitBoxInfo.num_axis[2]; z++) {
                HitBoxBoxSt *b = __GetBoxPointerForXYZ(x, y, z);
                __SetBoxBB(b, x, y, z);
                __SetNearBox(b, x, y, z);
            }
}

/* models (mflag & mdlFlag == 0, use & useFlag == 0) overlapping the box pos +- (width, height, width), at most
 * numArrMax */
void HitBoxGetInCylinderModel(float *pos, float width, float height, int mdlFlag, int useFlag, int *pMdlNum,
                              StModel **pArrMdl, int numArrMax)
{
    HitSprWk *w = HIT_WK;
    w->min[0] = pos[0] - width;
    w->min[1] = pos[1] - height;
    w->min[2] = pos[2] - width;
    w->max[0] = pos[0] + width;
    w->max[1] = pos[1] + height;
    w->max[2] = pos[2] + width;
    HitBoxBoxSt *box = shape_box();
    __GetInRegionBox(box, mdlFlag, useFlag, pMdlNum, pArrMdl, numArrMax);
    unsigned int f = __GetCollFlag(box);
    for (int i = 0; i < 7; i++)
        if (f & s_CollFlag[i]) __GetInRegionBox(box->pNearBox[i], mdlFlag, useFlag, pMdlNum, pArrMdl, numArrMax);
}

/* shape box = pos +- R (w = pos.w) */
static void sphere_box(const float *pos, float R)
{
    HitSprWk *w = HIT_WK;
    for (int i = 0; i < 3; i++) w->min[i] = pos[i] - R;
    w->min[3] = pos[3];
    for (int i = 0; i < 3; i++) w->max[i] = pos[i] + R;
    w->max[3] = pos[3];
}

void HitBoxGetInOutSphereModel(float *pos, float R, int mdlFlag, int useFlag, int *pInMdlNum, StModel **pInArrMdl,
                               int numInArrMax, int *pOutMdlNum, StModel **pOutArrMdl, int numOutArrMax)
{
    sphere_box(pos, R);
    HitBoxBoxSt *box = shape_box();
    __GetInOutRegionBox(box, mdlFlag, useFlag, pInMdlNum, pInArrMdl, numInArrMax, pOutMdlNum, pOutArrMdl,
                        numOutArrMax);
    unsigned int f = __GetCollFlag(box);
    for (int i = 0; i < 7; i++)
        if (f & s_CollFlag[i])
            __GetInOutRegionBox(box->pNearBox[i], mdlFlag, useFlag, pInMdlNum, pInArrMdl, numInArrMax, pOutMdlNum,
                                pOutArrMdl, numOutArrMax);
}

void HitBoxGetInSphereModel(float *pos, float R, int mdlFlag, int useFlag, int *pMdlNum, StModel **pArrMdl,
                            int numArrMax)
{
    sphere_box(pos, R);
    HitBoxBoxSt *box = shape_box();
    __GetInRegionBox(box, mdlFlag, useFlag, pMdlNum, pArrMdl, numArrMax);
    unsigned int f = __GetCollFlag(box);
    for (int i = 0; i < 7; i++)
        if (f & s_CollFlag[i]) __GetInRegionBox(box->pNearBox[i], mdlFlag, useFlag, pMdlNum, pArrMdl, numArrMax);
}

/* the scripted / event model (array scrid or eid set) nearest in x / z to the sight centre, among the models of
 * its box whose height range contains it (down to the AC's height below); *ppMdl untouched if none is nearer
 * than minSightDist2 */
void HitBoxGetNearSightModel(StModel **ppMdl, AcPlayerSt *pAC, float *sightCenter, float minSightDist2)
{
    int x = __NearAxisIndex(sightCenter[0], 0);
    int y = __NearAxisIndex(sightCenter[1], 1);
    int z = __NearAxisIndex(sightCenter[2], 2);
    for (HitBoxListSt *l = __GetBoxPointerForXYZ(x, y, z)->pList; l;) {
        StModel *m = l->pMdl;
        l = l->pNext;
        if (!m->array->scrid && !m->array->eid) continue;
        if (!(m->Ytop <= sightCenter[1])) continue;
        if (m->Ybottom < sightCenter[1] - pAC->tall) continue;
        if (m->use & 2) continue;
        float dx = m->mlist->sphere[0] - sightCenter[0], dz = m->mlist->sphere[2] - sightCenter[2];
        float d = dx * dx + dz * dz;
        if (d < minSightDist2) {
            *ppMdl = m;
            minSightDist2 = d;
        }
    }
}

/* the model under LP: up to retryCnt + 1 upward 150-unit line probes from LP; failing those, the model of LP's
 * box with the nearest centre, or the box's parent model. HitPos = the hit, or the last probe's top. */
StModel *HitBoxLineDecideParent(float *LP, int retryCnt, float *HitPos)
{
    PS2_FRAME(0xa0);
    const float STEP = 150.0f;
    float n[4] = {0.0f, 1.0f, 0.0f, 1.0f}, p[4];
    memcpy(p, LP, 16);
    int i = 0;
    for (; i < retryCnt + 1; i++) {
        if (HitBoxHitProcLine(p, n, STEP, 0, 0)) {
            if (HitPos) memcpy(HitPos, HIT_WK->info.hitpos, 16);
            return HIT_WK->info.pHitMdl;
        }
        for (int k = 0; k < 3; k++) p[k] = p[k] + n[k] * STEP;
    }
    if (HitPos) {
        memcpy(HitPos, LP, 16);
        HitPos[1] = HitPos[1] + STEP * (float)i;
    }
    float best = FMAX;
    StModel *found = 0;
    int x = __NearAxisIndex(LP[0], 0);
    int y = __NearAxisIndex(LP[1], 1);
    int z = __NearAxisIndex(LP[2], 2);
    HitBoxBoxSt *box = __GetBoxPointerForXYZ(x, y, z);
    for (HitBoxListSt *l = box->pList; l; l = l->pNext) {
        const float *s = l->pMdl->mlist->sphere;
        float dx = LP[0] - s[0], dy = LP[1] - s[1], dz = LP[2] - s[2];
        float d = dz * dz + (dx * dx + dy * dy);
        if (d < best) {
            found = l->pMdl;
            best = d;
        }
    }
    return found ? found : box->pParentMdl;
}

/* Y cylinder (bottom centre CyP, height CyH, radius CyR) for the floor / wall / water tests: the shape's box spans
 * CyP - (R, H, R) .. CyP + (R, 0, R) (sic: the box hangs below CyP although the cylinder tests use it as the
 * bottom); Center = CyP raised to the middle height */
static void cylinder_shape(const float *CyP, float CyH, float CyR)
{
    HitSprWk *w = HIT_WK;
    memcpy(w->cyl.P, CyP, 16);
    memcpy(w->cyl.Center, CyP, 16);
    w->cyl.Center[1] = CyP[1] - 0.5f * CyH;
    w->cyl.H = CyH;
    w->cyl.R = CyR;
    w->min[0] = w->cyl.P[0] - w->cyl.R;
    w->min[1] = w->cyl.P[1] - w->cyl.H;
    w->min[2] = w->cyl.P[2] - w->cyl.R;
    w->max[0] = w->cyl.P[0] + w->cyl.R;
    w->max[1] = w->cyl.P[1];
    w->max[2] = w->cyl.P[2] + w->cyl.R;
}

/* runs proc on the shape's box and the neighbours its box spills into */
template <class Proc>
static void each_box(Proc proc)
{
    HitBoxBoxSt *box = shape_box();
    proc(box);
    unsigned int f = __GetCollFlag(box);
    for (int i = 0; i < 7; i++)
        if (f & s_CollFlag[i]) proc(box->pNearBox[i]);
}

static void clear_result(void)
{
    memset(PS2_SPR(0), 0, 0x40);
    s_NowMarkIndex = 0;
}

/* water surface height at the cylinder -> *pYHit (CyP's height if none) and the model's mizuattribute */
int HitBoxHitProcWater(StModel *pChrMdl, float *CyP, float CyH, float CyR, float *pYHit)
{
    PS2_FRAME(0x90);
    HitSprWk *w = HIT_WK;
    int isHit = 0;
    *pYHit = CyP[1];
    clear_result();
    cylinder_shape(CyP, CyH, CyR);
    each_box([&](HitBoxBoxSt *b) { __HitProcWater(b, &isHit); });
    if (isHit) {
        *pYHit = w->hold[0];
        w->info.hitpos[1] = w->hold[0];
        pChrMdl->mizuattribute = w->info.color_B;
    } else {
        pChrMdl->mizuattribute = 0;
    }
    return isHit;
}

/* pushes the cylinder out of the walls: IntVec = the total push (CyP itself on no hit) */
int HitBoxHitProcKabe(StModel *pChrMdl, float *CyP, float CyH, float CyR, float *IntVec)
{
    PS2_FRAME(0x90);
    HitSprWk *w = HIT_WK;
    int isHit = 0;
    clear_result();
    memcpy(IntVec, CyP, 16);
    cylinder_shape(CyP, CyH, CyR);
    each_box([&](HitBoxBoxSt *b) { __HitProcKabe(b, &isHit); });
    if (isHit) {
        for (int i = 0; i < 3; i++) IntVec[i] = w->cyl.P[i] - CyP[i];
        IntVec[3] = w->cyl.P[3];
    }
    return isHit;
}

/* floor height at the cylinder -> *pYHit (CyP's height if none), the floor model (nowparent) and yukaattribute */
int HitBoxHitProcYuka(StModel *pChrMdl, float *CyP, float CyH, float CyR, float *pYHit)
{
    PS2_FRAME(0x90);
    HitSprWk *w = HIT_WK;
    int isHit = 0;
    *pYHit = CyP[1];
    clear_result();
    cylinder_shape(CyP, CyH, CyR);
    each_box([&](HitBoxBoxSt *b) { __HitProcYuka(b, &isHit); });
    if (isHit) {
        pChrMdl->nowparent = w->info.pHitMdl;
        *pYHit = w->hold[0];
        w->info.hitpos[1] = w->hold[0];
        pChrMdl->yukaattribute = w->info.color_B;
    }
    return isHit;
}

/* sphere against the stage: info.hitpos = the hit nearest to SP */
int HitBoxHitProcSphere(float *SP, float SR, int isAtc, int isChr)
{
    PS2_FRAME(0xb0);
    HitSprWk *w = HIT_WK;
    int isHit = 0;
    clear_result();
    memcpy(w->sphere.P, SP, 16);
    w->sphere.R = SR;
    for (int i = 0; i < 3; i++) w->min[i] = w->sphere.P[i] - w->sphere.R;
    for (int i = 0; i < 3; i++) w->max[i] = w->sphere.P[i] + w->sphere.R;
    each_box([&](HitBoxBoxSt *b) { __HitProcSphere(b, &isHit, isAtc, isChr); });
    if (isHit) memcpy(w->info.hitpos, w->hold, 16);
    return isHit;
}

/* line LP + LN * LL against the stage: info.hitpos / hitnorm = the nearest hit */
int HitBoxHitProcLine(float *LP, float *LN, float LL, int isAtc, int isChr)
{
    PS2_FRAME(0xb0);
    HitSprWk *w = HIT_WK;
    int isHit = 0;
    clear_result();
    memcpy(w->line.P, LP, 16);
    memcpy(w->line.N, LN, 16);
    for (int i = 0; i < 3; i++) w->line.V[i] = LN[i] * LL;
    w->line.V[3] = LN[3];
    w->line.L = LL;
    hit_line_box();   /* (the original also computes the midpoint P + V / 2 and drops it) */
    each_box([&](HitBoxBoxSt *b) { __HitProcLine(b, &isHit, isAtc, isChr); });
    if (isHit) memcpy(w->info.hitpos, w->hold, 16);
    return isHit;
}

/* re-sorts the moving models whose bounding box start moved to another box */
void HitBoxUpdateProc(void)
{
    for (HitBoxAbsHitListSt *e = s_pHitBoxUpdateList; e; e = e->pNext) {
        StModel *m = e->pMdl;
        if (m->use & 2) continue;
        const _map_model_list_st *ml = m->mlist;
        float ytop = m->Ytop, r = ml->R, z0 = ml->sphere[2] - r;
        int x = __NearAxisIndex(ml->sphere[0] - r, 0);
        int y = __NearAxisIndex(ytop, 1);
        int z = __NearAxisIndex(z0, 2);
        HitBoxBoxSt *box = __GetBoxPointerForXYZ(x, y, z), *old = e->pBox;
        if (box == old) continue;
        __RemoveMdl(old, m);
        for (int i = 0; i < 7; i++) {
            HitBoxBoxSt *nb = old->pNearBox[i];
            if (nb && __IsEntryMdl(nb, m)) __RemoveMdl(nb, m);
        }
        __AddABSModel(m, box, e);
        __SetOneBoxAtr(box);
        e->pBox = box;
    }
}

/* grid over the map bounding box widened to multiples of 160 */
void HitBoxInit(void)
{
    s_pHitBoxHead = 0;
    s_pHitBoxUpdateList = 0;
    ZeroMemory32b(&s_HitBoxInfo, 0xc);
    float mn[4], mx[4];
    HitSpcGetMapBB(mn, mx);
    for (int i = 0; i < 3; i++) {
        mn[i] = (float)((ps2_ftoi(mn[i] - 1.0f) - 0x9f) / 0xa0 * 0xa0);
        mx[i] = (float)((ps2_ftoi(1.0f + mx[i]) + 0x9f) / 0xa0 * 0xa0);
        s_HitBoxInfo.num_axis[i] = ps2_ftoi((1.0f + (mx[i] - mn[i])) / BOX);
    }
    mn[3] = mx[3] = 1.0f;
    memcpy(s_HitBoxInfo.mapMin, mn, 16);
    memcpy(s_HitBoxInfo.mapMax, mx, 16);
    s_HitBoxInfo.num_box = s_HitBoxInfo.num_axis[2] * (s_HitBoxInfo.num_axis[0] * s_HitBoxInfo.num_axis[1]);
    __MakeBox();
    int n = HitMapNum + HitObjNum;
    for (int i = 0; i < n; i++) {
        StModel *m = hit_model(i);
        HitSpcGetHitModelBoundBox(m);
        if (m->mflag & 1) __InsertUpdateMdl(m);
        else __EntryOneMdl(m);
    }
    for (int i = 0; i < s_HitBoxInfo.num_box; i++) __SetOneBoxParentMdl(&s_pHitBoxHead[i]);
    for (int i = 0; i < s_HitBoxInfo.num_box; i++) __SetOneBoxAtr(&s_pHitBoxHead[i]);
}
