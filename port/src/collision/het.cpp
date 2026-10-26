/* src/collision/het.cpp - octree over the stage's hit models (whole file), used for line tests against the stage
 * (HetProcLine: bullets, lock-on sight, cameras). The root (depth 1) spans s_Region (Center +- Range); every node
 * down to depth 4 has 8 children, child [i][j][k] lies on the -/+ side of x/y/z. A model is listed in the deepest
 * node that contains its bounding volume (OctreeListSt per model, StModel.pOctList); mdl_num counts a node's own
 * models, all_mdl_num those of its whole subtree. Models with mflag bit 0 move and are re-sorted every frame
 * (HetUpdateProc, s_pOctListUpdate). HetInit first tunes the region size per axis by bisection so the models
 * spread evenly over the leaves (__CalcRangeByDichotomy). Line state and results live in the scratchpad work
 * area (hitwk.h). */
#include "acnb/sdk.h"
#include "hitwk.h"

static const int DEPTH_MAX = 4;

/* 8^n for n >= 1 */
static inline int pow8(int n)
{
    int c = 8;
    while (--n) c <<= 3;
    return c;
}

static inline void node_counts_add(OctreeNodeSt *n, int d)
{
    n->mdl_num += d;
    n->all_mdl_num += d;
    for (OctreeNodeSt *p = n->pParentNode; p; p = p->pParentNode) p->all_mdl_num += d;
}

/* the model's volume (its sphere in x / z, Ytop..Ybottom in y) lies inside the node's box */
static inline bool mdl_in_node(const OctreeNodeSt *n, const StModel *m)
{
    const float *s = m->mlist->sphere;
    float r = s[3];
    if (!(s[0] + r <= n->max_bb[0])) return false;
    if (!(m->Ybottom <= n->max_bb[1])) return false;
    if (!(s[2] + r <= n->max_bb[2])) return false;
    if (s[0] - r < n->min_bb[0]) return false;
    if (m->Ytop < n->min_bb[1]) return false;
    return !(s[2] - r < n->min_bb[2]);
}

/* the model bounding box from HitSpcGetHitModelBoundBox lies inside the node's box */
static inline bool bb_in_node(const OctreeNodeSt *n)
{
    const HitSprWk *w = HIT_WK;
    for (int k = 0; k < 3; k++)
        if (w->bb.min[k] < n->min_bb[k]) return false;
    for (int k = 0; k < 3; k++)
        if (!(w->bb.max[k] <= n->max_bb[k])) return false;
    return true;
}

/* the node's box overlaps the line's bounding box */
static inline bool node_on_line(const OctreeNodeSt *n)
{
    const HitSprWk *w = HIT_WK;
    for (int k = 0; k < 3; k++)
        if (!(w->min[k] <= n->max_bb[k])) return false;
    for (int k = 0; k < 3; k++)
        if (w->max[k] < n->min_bb[k]) return false;
    return true;
}

static inline OctreeNodeSt *child_of(OctreeNodeSt *n, int idx) { return n->pChildNode[(idx & 4) >> 2][(idx & 2) >> 1][idx & 1]; }

/* sum over the tree of (models per node)^2 weighted by the leaf count below the node; leaves count their
 * deviation from the average instead: how unevenly the models are spread */
static void __GetSumDisp(OctreeNodeSt *pNode, float avg_mdl_num, float *pSumDisp)
{
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 2; k++)
                if (OctreeNodeSt *c = pNode->pChildNode[i][j][k]) __GetSumDisp(c, avg_mdl_num, pSumDisp);
    int n = DEPTH_MAX - pNode->depth;
    int leaves = n > 0 ? pow8(n) : 1;
    float f;
    if (pNode->depth == DEPTH_MAX) {
        f = (float)pNode->mdl_num - avg_mdl_num;
        f = f * f;
    } else {
        f = (float)pNode->mdl_num * (float)pNode->mdl_num;
    }
    *pSumDisp = *pSumDisp + f * (float)leaves;
}

static void visit_children(OctreeNodeSt *pNode, int *pIsHit, int flag)
{
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 2; k++) {
                OctreeNodeSt *c = pNode->pChildNode[i][j][k];
                if (!c->all_mdl_num || !node_on_line(c)) continue;
                if (c->mdl_num == 0) __HetProcReturnNode(c, pIsHit, flag);
                else __HetProcLineNode(c, pIsHit, flag);
            }
}

/* a node without own models: only descend */
static void __HetProcReturnNode(OctreeNodeSt *pNode, int *pIsHit, int flag)
{
    PS2_FRAME(0x90);
    if (pNode->depth < DEPTH_MAX) visit_children(pNode, pIsHit, flag);
}

/* a hit on triangle k of strip ph (vertex refs v) of model m: the line now ends at the hit */
static void line_hit(StModel *m, const struct_de4d68f5 *ph, const HmdPrimSt *v, int k)
{
    HitSprWk *w = HIT_WK;
    float t = w->info.tt1;
    for (int i = 0; i < 3; i++) w->line.V[i] = w->line.V[i] * t;
    w->line.L = w->line.L * t;
    w->info.color_B = (unsigned char)v->b;
    HitFuncSetHitMdl(m);
    hit_line_box();
    hit_line_normal(ph, k);
}

/* tests the line against the hit meshes of the node's models, then descends */
static void __HetProcLineNode(OctreeNodeSt *pNode, int *pIsHit, int flag)
{
    PS2_FRAME(0xe0);
    HitSprWk *w = HIT_WK;
    for (OctreeListSt *l = pNode->pOctList; l;) {
        StModel *m = l->pMdl;
        HmdHeadSt *h = m->hmd;
        l = l->pNext;
        if (m->use & 2) continue;
        if ((flag & 2) && (m->use & 0x4000)) continue;
        if ((flag & 1) && (m->array->mflag & 0x80000)) continue;
        if (h->v_num < 4) continue;
        if (w->max[1] < m->Ytop || !(w->min[1] <= m->Ybottom)) continue;
        float *s = m->mlist->sphere, r = s[3];
        if (w->max[0] < s[0] - r || w->max[2] < s[2] - r) continue;
        if (!(w->min[0] <= s[0] + r) || !(w->min[2] <= s[2] + r)) continue;
        HitFuncSphereLineCheckSPR(s, r);
        if (!w->info.hit) continue;

        hit_mesh_to_spr(m);
        struct_de4d68f5 *ph = h->php;
        for (unsigned int np = h->p_num;; ph = prim_next(ph)) {
            HmdPrimSt *v = prim_verts(ph);
            if (v->b != 7 && __SeekPrimInOut(v, ph->pv_num)) {
                for (int t = ph->pv_num - 2, k = 0;; v++, k++) {
                    HitFuncLinePolygonSPR(*v[0].v_id, *v[1].v_id, *v[2].v_id);
                    if (w->info.hit) {
                        ++*pIsHit;
                        line_hit(m, ph, v, k);
                    }
                    if (--t == 0) break;
                }
            }
            if (--np == 0) break;
        }
    }
    if (pNode->depth < DEPTH_MAX) visit_children(pNode, pIsHit, flag);
}

/* the strip's bounding box overlaps the line's */
static int __SeekPrimInOut(HmdPrimSt *pVert, int VertNum) { return hit_prim_in_box(pVert, VertNum); }

static int __SeekMdlInOut(OctreeNodeSt *pNode, StModel *pMdl) { return mdl_in_node(pNode, pMdl); }

static int __SeekBoundBoxInOut(OctreeNodeSt *pNode) { return bb_in_node(pNode); }

/* deepest node containing the model, following the child nearest to its centre */
static OctreeNodeSt *__GetMdlNode(OctreeNodeSt *pNode, StModel *pMdl)
{
    if (pNode->depth >= DEPTH_MAX) return pNode;
    OctreeNodeSt *c = child_of(pNode, __GetNearDistNode(pNode, pMdl->mlist->sphere));
    if (!mdl_in_node(c, pMdl)) return pNode;
    if (c->depth >= DEPTH_MAX) return c;
    OctreeNodeSt *g = child_of(c, __GetNearDistNode(c, pMdl->mlist->sphere));
    if (!__SeekMdlInOut(g, pMdl)) return c;
    return __GetMdlNode(g, pMdl);
}

/* index (i << 2 | j << 1 | k) of the child whose centre is nearest */
static int __GetNearDistNode(OctreeNodeSt *pNode, float *Center)
{
    float best = ps2_u2f(0x7f7fffff);
    int idx = 0;   /* (the original returns a stale register if no distance is below FLT_MAX: NaN only) */
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 2; k++) {
                const float *l = pNode->pChildNode[i][j][k]->loc;
                float dx = l[0] - Center[0], dy = l[1] - Center[1], dz = l[2] - Center[2];
                float d = dz * dz + (dx * dx + dy * dy);
                if (d < best) {
                    best = d;
                    idx = k | i << 2 | j << 1;
                }
            }
    return idx;
}

static void __InsertUpdateList(StModel *pMdl)
{
    if (!s_pOctListUpdate) {
        s_pOctListUpdate = (OctreeListSt *)Memm_Alloc(1, 0x10, 0x10);
        OctreeListSt *l = s_pOctListUpdate;
        l->pMdl = pMdl;
        l->pNode = 0;
        l->pBack = 0;
        l->pNext = 0;
        return;
    }
    OctreeListSt *p = s_pOctListUpdate;
    while (p->pNext) p = p->pNext;
    p->pNext = (OctreeListSt *)Memm_Alloc(1, 0x10, 0x10);
    OctreeListSt *l = p->pNext;
    l->pMdl = pMdl;
    l->pNode = 0;
    l->pBack = p;
    l->pNext = 0;
}

/* appends the model's (existing) list entry to the node */
static void __AddModelList(OctreeNodeSt *pNode, StModel *pMdl)
{
    if (!pNode->pOctList) {
        pNode->pOctList = pMdl->pOctList;
        OctreeListSt *l = pNode->pOctList;
        l->pNode = pNode;
        l->pBack = 0;
        l->pNext = 0;
        pNode->pOctList = l;
    } else {
        OctreeListSt *p = pNode->pOctList;
        while (p->pNext) p = p->pNext;
        p->pNext = pMdl->pOctList;
        OctreeListSt *l = p->pNext;
        l->pNode = pNode;
        l->pBack = p;
        l->pNext = 0;
    }
    node_counts_add(pNode, 1);
}

/* appends a new list entry for the model to the node */
static void __InsertModelList(OctreeNodeSt *pNode, StModel *pMdl)
{
    OctreeListSt *l;
    if (!pNode->pOctList) {
        l = (OctreeListSt *)Memm_Alloc(1, 0x10, 0x10);
        pMdl->pOctList = l;
        l->pMdl = pMdl;
        l->pNode = pNode;
        l->pBack = 0;
        l->pNext = 0;
        pNode->pOctList = l;
    } else {
        OctreeListSt *p = pNode->pOctList;
        while (p->pNext) p = p->pNext;
        p->pNext = (OctreeListSt *)Memm_Alloc(1, 0x10, 0x10);
        l = p->pNext;
        pMdl->pOctList = l;
        l->pMdl = pMdl;
        l->pNode = pNode;
        l->pBack = p;
        l->pNext = 0;
    }
    node_counts_add(pNode, 1);
}

static float sum_disp(void)
{
    float sum = 0.0f;
    __GetSumDisp(s_pOctNodeHead, (float)(HitMapNum + HitObjNum) / (float)pow8(DEPTH_MAX - 1), &sum);
    return sum;
}

/* bisection on one axis of the region's half size: tries NowRange[axis] +- divRange and keeps the side that
 * spreads the models more evenly, halving the step when neither improves; stops below half the average model
 * size */
static float __CalcRangeByDichotomy(float *NowRange, float divRange, float nowDisp, int axis)
{
    float avg[4];
    HitSpcGetMdlAvgSize(avg);
    if (divRange <= 0.5f * avg[axis]) return NowRange[axis];
    float up[4], dn[4];
    memcpy(up, NowRange, 16);
    up[axis] = up[axis] + divRange;
    memcpy(dn, NowRange, 16);
    dn[axis] = dn[axis] - divRange;
    HetRemakeOctree(s_Region.Center, up);
    float dup = sum_disp();
    HetRemakeOctree(s_Region.Center, dn);
    float ddn = sum_disp();
    if (dup < ddn) {
        if (dup < nowDisp) return __CalcRangeByDichotomy(up, divRange, dup, axis);
        return __CalcRangeByDichotomy(NowRange, 0.5f * divRange, nowDisp, axis);
    }
    if (ddn < nowDisp) return __CalcRangeByDichotomy(dn, divRange, ddn, axis);
    return __CalcRangeByDichotomy(NowRange, 0.5f * divRange, nowDisp, axis);
}

/* the deepest node below pNode that contains the model's bounding box (from HitSpcGetHitModelBoundBox): looks at
 * the child nearest to the model and that child's nearest child, then continues from the grandchild */
static OctreeNodeSt *bb_node(OctreeNodeSt *pNode, StModel *pMdl, OctreeNodeSt **next)
{
    *next = 0;
    if (pNode->depth >= DEPTH_MAX) return pNode;
    OctreeNodeSt *c = child_of(pNode, __GetNearDistNode(pNode, pMdl->mlist->sphere));
    if (!bb_in_node(c)) return pNode;
    if (c->depth >= DEPTH_MAX) return c;
    OctreeNodeSt *g = child_of(c, __GetNearDistNode(c, pMdl->mlist->sphere));
    if (!__SeekBoundBoxInOut(g)) return c;
    *next = g;
    return 0;
}

static void __RemakeOctree(OctreeNodeSt *pNode, StModel *pMdl)
{
    OctreeNodeSt *g, *n = bb_node(pNode, pMdl, &g);
    if (g) __RemakeOctree(g, pMdl);
    else __AddModelList(n, pMdl);
}

static void __MakeOctree(OctreeNodeSt *pNode, StModel *pMdl)
{
    OctreeNodeSt *g, *n = bb_node(pNode, pMdl, &g);
    if (g) __MakeOctree(g, pMdl);
    else __InsertModelList(n, pMdl);
}

/* child centres = parent +- half the cell size at this depth; boxes 1.5 times the cell (cells overlap so a model
 * near a border still fits one) */
static void child_geometry(OctreeNodeSt *pNode, OctreeNodeSt *c, int i, int j, int k, const float *half)
{
    c->loc[0] = pNode->loc[0] + half[0] * (i ? 1.0f : -1.0f);
    c->loc[1] = pNode->loc[1] + half[1] * (j ? 1.0f : -1.0f);
    c->loc[2] = pNode->loc[2] + half[2] * (k ? 1.0f : -1.0f);
    c->loc[3] = 1.0f;
}

static void child_box(OctreeNodeSt *c, const float *ext)
{
    for (int m = 0; m < 3; m++) c->min_bb[m] = c->loc[m] - ext[m];
    c->min_bb[3] = c->loc[3];
    for (int m = 0; m < 3; m++) c->max_bb[m] = c->loc[m] + ext[m];
    c->max_bb[3] = c->loc[3];
}

static void cell_size(const OctreeNodeSt *pNode, float *half, float *ext)
{
    half[0] = s_Region.Range[0] / (float)(1 << pNode->depth);
    half[1] = s_Region.Range[1] / (float)(1 << pNode->depth);
    half[2] = s_Region.Range[2] / (float)(1 << pNode->depth);
    half[3] = 1.0f;
    for (int m = 0; m < 3; m++) ext[m] = half[m] * 1.5f;
}

/* re-lays the existing tree for a new s_Region */
static void __RemakeNode(OctreeNodeSt *pNode)
{
    if (pNode->depth >= DEPTH_MAX) return;
    float half[4], ext[4];
    cell_size(pNode, half, ext);
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 2; k++) {
                OctreeNodeSt *c = pNode->pChildNode[i][j][k];
                child_geometry(pNode, c, i, j, k, half);
                c->mdl_num = 0;
                c->all_mdl_num = 0;
                c->pOctList = 0;
                child_box(c, ext);
                __RemakeNode(c);
            }
}

static void __MakeNode(OctreeNodeSt *pNode)
{
    if (pNode->depth >= DEPTH_MAX) return;
    float half[4], ext[4];
    cell_size(pNode, half, ext);
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 2; k++) {
                OctreeNodeSt *c = (OctreeNodeSt *)Memm_Alloc(1, 0x60, 0x10);
                child_geometry(pNode, c, i, j, k, half);
                c->mdl_num = 0;
                c->all_mdl_num = 0;
                c->depth = pNode->depth + 1;
                c->pOctList = 0;
                c->pParentNode = pNode;
                memset(c->pChildNode, 0, sizeof c->pChildNode);
                child_box(c, ext);
                pNode->pChildNode[i][j][k] = c;
                __MakeNode(c);
            }
}

/* Line loc + lvec * L against the stage. dest = end point, or the nearest hit (HIT_WK->info: hit, hit model,
 * normal; flag 1 = skip models whose array has mflag 0x80000, 2 = skip models with use 0x4000). Returns the hit
 * polygon's attribute. */
int HetProcLine(float *loc, float *lvec, float L, float *dest, int flag)
{
    PS2_FRAME(0x50);
    HitSprWk *w = HIT_WK;
    OctreeNodeSt *root = s_pOctNodeHead;
    int isHit = 0;
    memcpy(w->line.P, loc, 16);
    memcpy(w->line.N, lvec, 16);
    for (int i = 0; i < 3; i++) w->line.V[i] = lvec[i] * L;
    w->line.V[3] = lvec[3];
    w->line.L = L;
    float e[4];
    for (int i = 0; i < 3; i++) e[i] = w->line.P[i] + w->line.V[i];
    e[3] = w->line.P[3];
    memcpy(dest, e, 16);
    for (int i = 0; i < 3; i++) {   /* (from loc and dest as given: the original reloads both) */
        w->min[i] = vu_min(loc[i], dest[i]);
        w->max[i] = vu_max(loc[i], dest[i]);
    }
    w->min[3] = PS2_STALE_VF(20, 3);
    w->max[3] = PS2_STALE_VF(21, 3);
    __HetProcLineNode(root, &isHit, flag);
    if (isHit) {
        w->info.hit = 1;
        for (int i = 0; i < 3; i++) w->info.hitpos[i] = w->line.P[i] + w->line.V[i];
        w->info.hitpos[3] = w->line.P[3];
        memcpy(dest, w->info.hitpos, 16);
    } else {
        w->info.hit = 0;
    }
    return w->info.color_B;
}

int HetProcPoint2Point(float *loc1, float *loc2, float *dest, int flag)
{
    PS2_FRAME(0x40);
    float p[4], d[4];
    memcpy(p, loc1, 16);
    for (int i = 0; i < 3; i++) d[i] = loc2[i] - loc1[i];
    float x = d[0] * d[0], y = d[1] * d[1], z = d[2] * d[2];
    float len = ps2_sqrt(z + (x + y));
    float q = vu_rcp_len3(d);
    for (int i = 0; i < 3; i++) d[i] = d[i] * q;
    d[3] = 0.0f;
    HetProcLine(p, d, len, dest, flag);
    return HIT_WK->info.hit != 0;
}

/* re-sorts the moving models into the nodes that now contain them */
void HetUpdateProc(void)
{
    OctreeNodeSt *root = s_pOctNodeHead;
    for (OctreeListSt *u = s_pOctListUpdate; u; u = u->pNext) {
        StModel *m = u->pMdl;
        if (m->use & 2) continue;
        OctreeNodeSt *n = __GetMdlNode(root, m);
        if (n == u->pNode) continue;
        OctreeListSt *l = m->pOctList, *back = l->pBack, *next = l->pNext;
        OctreeNodeSt *old = l->pNode;
        if (!back) old->pOctList = next;
        else back->pNext = next;
        if (next) next->pBack = back;
        node_counts_add(old, -1);
        __AddModelList(n, m);
        u->pNode = n;
    }
}

static void root_box(OctreeNodeSt *n)
{
    for (int m = 0; m < 3; m++) n->min_bb[m] = n->loc[m] - s_Region.Range[m];
    n->min_bb[3] = n->loc[3];
    for (int m = 0; m < 3; m++) n->max_bb[m] = n->loc[m] + s_Region.Range[m];
    n->max_bb[3] = n->loc[3];
}

/* re-lays the tree over a new region and re-sorts all hit models */
void HetRemakeOctree(float *Center, float *Range)
{
    memmove(s_Region.Center, Center, 16);
    memmove(s_Region.Range, Range, 16);
    OctreeNodeSt *root = s_pOctNodeHead;
    memcpy(root->loc, s_Region.Center, 16);
    root->mdl_num = 0;
    root->all_mdl_num = 0;
    root->pOctList = 0;
    root_box(root);
    __RemakeNode(root);
    int n = HitMapNum + HitObjNum;
    for (int i = 0; i < n; i++) {
        StModel *m = hit_model(i);
        HitSpcGetHitModelBoundBox(m);
        __RemakeOctree(root, m);
    }
}

void HetBuildOctree(float *Center, float *Range)
{
    s_pOctNodeHead = 0;
    s_pOctListUpdate = 0;
    memmove(s_Region.Center, Center, 16);
    memmove(s_Region.Range, Range, 16);
    s_pOctNodeHead = (OctreeNodeSt *)Memm_Alloc(1, 0x60, 0x10);
    OctreeNodeSt *root = s_pOctNodeHead;
    memcpy(root->loc, s_Region.Center, 16);
    root->mdl_num = 0;
    root->all_mdl_num = 0;
    root->depth = 1;
    root->pOctList = 0;
    root->pParentNode = 0;
    memset(root->pChildNode, 0, sizeof root->pChildNode);
    root_box(root);
    __MakeNode(root);
    int n = HitMapNum + HitObjNum;
    for (int i = 0; i < n; i++) {
        StModel *m = hit_model(i);
        HitSpcGetHitModelBoundBox(m);
        if (m->mflag & 1) __InsertUpdateList(m);
        __MakeOctree(root, m);
    }
    for (OctreeListSt *u = s_pOctListUpdate; u; u = u->pNext) u->pNode = u->pMdl->pOctList->pNode;
}

void HetInit(float *Center, float *Range)
{
    HetBuildOctree(Center, Range);
    float now = sum_disp();
    float r[4];
    memcpy(r, Range, 16);
    for (int i = 0; i < 3; i++) r[i] = __CalcRangeByDichotomy(r, 0.5f * r[i], now, i);
    HetRemakeOctree(Center, r);
}
