/* src/collision/hit.cpp - the older hit system (whole file): per-model hit info and the model link lists built at
 * stage load (DF_HIT_HitInit, DF_HIT_BuildHitLink), and the floor ("parent") search under a character's Y-cylinder
 * (DF_HIT_DecideParent over all stage models, DF_HIT_HitProc_YUKA over the current parent's link list). Scratchpad
 * layout HitOldWk (hitwk.h); y grows downwards. */
#include "acnb/sdk.h"
#include "hitwk.h"

/* loc + len along y through DF_HIT_HitProc_Line (hitline.c) */
int DF_HIT_HitProc_2Point(float *loc, float *loc2, float *dest)
{
    PS2_FRAME(0x60);
    PS2_STACK(float[4], d, 0x50);
    for (int i = 0; i < 3; i++) d[i] = loc2[i] - loc[i];
    d[3] = loc2[3];
    PS2_VF_MIRROR(16, d);
    PS2_VF_MIRROR(17, loc);
    float x = d[0] * d[0], y = d[1] * d[1], z = d[2] * d[2];
    float len = ps2_sqrt(z + (x + y));
    sceVu0Normalize(d, d);
    DF_HIT_HitProc_Line(loc, d, len, dest);
    return HIT_OWK->hit;
}

/* standing on another character: the highest (smallest y) character top under the cylinder of AC `id`;
 * dest = loc with dest.y = that top (or loc.y + len); returns the first character found */
StModel *DF_HIT_HitProc_YUKA_Char(int id, float *loc, float len, float R, float *dest)
{
    PS2_FRAME(0xa0);
    HitOldWk *w = HIT_OWK;
    StModel *res = 0;
    int found = 0;
    w->R = R;
    memcpy(dest, loc, 16);
    memcpy(w->loc, loc, 16);
    w->loc[1] = w->loc[1] + len;
    w->len = len;
    float lim = w->loc[1];
    if (MyAC[id].use & 2) return 0;
    auto test = [&](StModel *m) {
        w->hit = 0;
        DF_HIT_CylinderCylinder(m->loc, m->R, m->tall, w->loc, w->R, w->len);
        if (!w->hit || !(w->pos[1] < lim) || !(loc[1] < m->loc[1])) return;
        lim = m->loc[1] - m->tall;
        if (!found) {
            res = m;
            found = 1;
        }
    };
    StModel *m = MyAC;
    for (int i = 0; i < NowPlayerNum; i++, m++)
        if (i != id && !(m->use & 2)) test(m);
    int cnum = StgInfo.cnum;
    m = Chr;
    for (int i = 0; i < cnum; i++, m++)
        if (!(m->use & 2) && !(m->use & 0x40)) test(m);
    dest[1] = lim;
    return res;
}

/* the model's bounding circle (xz, radius R + model radius) contains the cylinder axis */
static inline int hit_near_xz(const HitOldWk *w, const StModel *m)
{
    const _map_model_list_st *ml = m->mlist;
    float r = w->R + ml->R;
    float dx = w->loc[0] - ml->sphere[0];
    if (!(dx <= r) || dx < -r) return 0;
    float dz = w->loc[2] - ml->sphere[2];
    if (!(dz <= r) || dz < -r) return 0;
    return dx * dx + dz * dz <= r * r;
}

/* HitProc_Line straight down from loc when no floor polygon was found */
static void hit_parent_fallback(StModel *acp, float *loc, float len, float *dir, float *out)
{
    dir[0] = 0.0f;
    dir[1] = 1.0f;
    dir[2] = 0.0f;
    dir[3] = 1.0f;
    DF_HIT_HitProc_Line(loc, dir, len, out);
    if (HIT_OWK->hit) acp->nowparent = HIT_OWK->line_mdl;
}

/* Floor search through the current parent's link list (the parent and the models near it). Also keeps the last
 * 7 models stood on in HIT_OWK->hist, the floor normal in HIT_OWK->norm and the character light list. */
StModel *DF_HIT_HitProc_YUKA(StModel *acp, float *loc, float len, float R, float *dest)
{
    PS2_FRAME(0x120);
    PS2_STACK(StModel *, res, 0xcc);
    PS2_STACK(int, first, 0xd0);
    PS2_STACK(int, found, 0xb0);
    PS2_STACK(unsigned int, np, 0xe0);
    PS2_STACK(float[4], norm, 0x110);
    PS2_STACK(float[4], dir, 0x100);
    PS2_STACK(float[4], out, 0xf0);
    if (!acp->nowparent) return 0;
    HitOldWk *w = HIT_OWK;
    int nhist = 0;
    w->R = R;
    first = 0;
    res = 0;
    w->RR = R * R;
    memcpy(dest, loc, 16);
    memcpy(w->loc, loc, 16);
    w->len = len;
    w->lim = w->loc[1] + len;
    acp->LocY = w->lim;
    w->loc[1] = loc[1];
    ZeroMemory32b(w->hist, 8);
    for (HitModelLinkSt *hl = acp->nowparent->hlfirst; hl; hl = hl->next) {
        StModel *m = hl->mp;
        if (m->use & 2) continue;
        HmdHeadSt *hmd = m->hmd;
        if (!hmd->yuka) continue;
        if (!hit_near_xz(w, m)) continue;
        if (!(w->loc[1] <= m->Ybottom) || w->lim < m->Ytop) continue;
        hit_mesh_to_spr(m);
        found = 0;
        np = hmd->p_num;
        struct_de4d68f5 *ph = hmd->php;
        do {
            HmdPrimSt *v = prim_verts(ph);
            int n = ph->pv_num - 2;
            if (v[0].g == 0xff && !(v[0].b & 1)) {
                do {
                    if (!DF_HIT_RectHitCheck_YUKA(*v[0].v_id, *v[1].v_id, *v[2].v_id)) {
                        DF_HIT_CylinderPolygon_YUKA(*v[0].v_id, *v[1].v_id, *v[2].v_id, w->loc);
                        if (w->hit) {
                            found = 1;
                            if (w->pos[1] < w->lim) {
                                acp->LocY = w->pos[1];
                                w->lim = w->pos[1];
                                acp->nowparent = m;
                                acp->yukaattribute = (unsigned char)v[0].b;
                                if (!first) {
                                    res = m;
                                    first = found;
                                }
                                float e1[4], e2[4];
                                for (int i = 0; i < 3; i++) {
                                    e1[i] = (*v[0].v_id)[i] - (*v[1].v_id)[i];
                                    e2[i] = (*v[2].v_id)[i] - (*v[1].v_id)[i];
                                }
                                vu_cross(w->norm, e1, e2);
                                w->norm[3] = (*v[0].v_id)[3];
                                PS2_VF_MIRROR(16, w->norm);
                                PS2_VF_MIRROR(17, *v[1].v_id);
                                PS2_VF_MIRROR(18, *v[2].v_id);
                                hit_vf_xyz(19, e1);
                                hit_vf_xyz(20, e2);
                                sceVu0Normalize(w->norm, w->norm);
                                if (!(w->norm[1] <= 0.0f)) {
                                    w->norm[0] = -w->norm[0];
                                    w->norm[1] = -w->norm[1];
                                    w->norm[2] = -w->norm[2];
                                }
                                memcpy(norm, w->norm, 16);
                            }
                        }
                    }
                    v++;
                } while (--n);
            } else {
                v += n;
            }
            ph = (struct_de4d68f5 *)(v + 2);
        } while (--np);
        if (found) {
            if (nhist >= 7)
                for (int i = 0; i < 7; i++) w->hist[i] = w->hist[i + 1];
            w->hist[nhist++] = m;
            if (nhist >= 7) nhist = 7;
        }
    }
    if (w->keep & 1) {
        dest[1] = w->lim;
        memcpy(w->norm, norm, 16);
        return res;
    }
    if (!first) hit_parent_fallback(acp, loc, len, dir, out);
    dest[1] = w->lim;
    if (acp->llist != acp->nowparent->mlist->lp_chrlight) {
        acp->llistbk = acp->llist;
        acp->llist = acp->nowparent->mlist->lp_chrlight;
        acp->ylfrm = 0;
    } else {
        acp->ylfrm = acp->ylfrm + 1;
        if (acp->ylfrm >= 13) acp->ylfrm = 12;
    }
    memcpy(w->norm, norm, 16);
    return res;
}

/* bounding data of a stage model: Ytop / Ybottom, mlist sphere (centre, xz radius R, 3D radius in .w),
 * localcenter; registers hit effects (r = g = 0x40 polygons). 0 = not a hit model (fewer than 4 vertices and
 * no script / event button). */
static int init_hitinfo(StModel *mp)
{
    PS2_FRAME(0x90);
    PS2_STACK(float[4], lc, 0x60);
    PS2_STACK(float[4], wc, 0x70);
    PS2_STACK(float[4], d, 0x80);
    HmdHeadSt *hmd = mp->hmd;
    HitOldWk *w = HIT_OWK;
    const float (*mat)[4] = mp->matrix2;
    const float BIG = 1e+09f;
    float lmin[3] = {BIG, BIG, BIG}, lmax[3] = {-BIG, -BIG, -BIG};
    float wmin[3] = {BIG, BIG, BIG}, wmax[3] = {-BIG, -BIG, -BIG};
    wc[3] = 1.0f;
    const float (*vp)[4] = hmd->vp;
    unsigned int nv = hmd->v_num;
    for (unsigned int i = 0; i < nv; i++) {
        for (int k = 0; k < 3; k++) {
            if (vp[i][k] < lmin[k]) lmin[k] = vp[i][k];
            if (!(vp[i][k] <= lmax[k])) lmax[k] = vp[i][k];
        }
        vu_apply(w->vtx[i], mat, vp[i]);
        for (int k = 0; k < 3; k++) {
            if (w->vtx[i][k] < wmin[k]) wmin[k] = w->vtx[i][k];
            if (!(w->vtx[i][k] <= wmax[k])) wmax[k] = w->vtx[i][k];
        }
    }
    mp->Ytop = wmin[1];
    mp->Ybottom = wmax[1];
    for (int k = 0; k < 3; k++) {
        wc[k] = 0.5f * (wmin[k] + wmax[k]);
        lc[k] = 0.5f * (lmin[k] + lmax[k]);
    }
    float rxz = -BIG, r3 = -BIG;
    for (unsigned int i = 0; i < nv; i++) {
        for (int k = 0; k < 3; k++) d[k] = wc[k] - w->vtx[i][k];
        d[3] = wc[3];
        float t = d[0] * d[0] + d[2] * d[2];
        if (!(t <= rxz)) rxz = t;
        t = (d[0] * d[0] + d[1] * d[1]) + d[2] * d[2];
        if (!(t <= r3)) r3 = t;
    }
    mp->mlist->R = ps2_sqrt(rxz);
    mp->mlist->sphere[3] = ps2_sqrt(r3);
    mp->localcenter[0] = lc[0];
    mp->localcenter[1] = lc[1];
    mp->localcenter[2] = lc[2];
    mp->localcenter[3] = 1.0f;
    vu_apply(wc, mat, mp->localcenter);
    for (int j = 0; j < 4; j++) PS2_VF_MIRROR(1 + j, mat[j]);
    PS2_VF_MIRROR(16, wc);
    if (nv) PS2_VF_MIRROR(17, w->vtx[nv - 1]);
    mp->mlist->sphere[0] = wc[0];
    mp->mlist->sphere[1] = wc[1];
    mp->mlist->sphere[2] = wc[2];
    if (hmd->v_num < 4) return mp->array->scrid != 0 || Gfunc_IsEventBtnExec(mp) != 0;
    struct_de4d68f5 *ph = hmd->php;
    unsigned int np = hmd->p_num;
    do {
        HmdPrimSt *v = prim_verts(ph);
        int n = ph->pv_num - 2;
        do {
            if (v->r == 0x40 && v->g == 0x40) {
                DF_EFF2_HitEffectInit(mp, &HitEffect[HitEffectNum], hmd, v);
                HitEffectNum = HitEffectNum + 1;
            }
            v++;
        } while (--n);
        ph = (struct_de4d68f5 *)(v + 2);
    } while (--np);
    return 1;
}

/* append the models of hmp[0..num) that mp's characters can reach to mp's link list: models with floor / wall /
 * water polygons within 40 of mp's bounding circle whose group is mp's group or one of its layer groups (or
 * either model flagged mflag 1) */
static void link_hit_model(StModel *mp, StModel **hmp, int num)
{
    float r0 = 40.0f + mp->mlist->R;
    for (int i = 0; i < num; i++, hmp++) {
        StModel *m = *hmp;
        HmdHeadSt *hmd = m->hmd;
        if (!hmd->yuka && !hmd->kabe && !hmd->water) continue;
        if (mp == m) continue;
        if (!(mp->mflag & 1) && !(m->mflag & 1)) {
            _map_model_list_st *ml = m->mlist;
            float r = r0 + ml->R;
            float dx = mp->mlist->sphere[0] - ml->sphere[0];
            if (!(dx <= r) || dx < -r) continue;
            float dz = mp->mlist->sphere[2] - ml->sphere[2];
            if (!(dz <= r) || dz < -r) continue;
            if (!(dx * dx + dz * dz <= r * r)) continue;
            unsigned int g = mp->array->groupid;
            unsigned char *layer = Array->group[g].layer;
            int cnt = layer[0x3f];
            unsigned char *mark = (unsigned char *)PS2_SPR(0);
            ZeroMemory32b(mark, 0x40);
            mark[g] = 1;
            for (int j = 0; j < cnt; j++) mark[layer[j]] = 1;
            if (!(signed char)mark[m->array->groupid]) continue;
        }
        HitModelLinkSt *node = (HitModelLinkSt *)Memm_Alloc(1, 0x10, 0x10);
        node->mp = m;
        node->next = 0;
        mp->hllast->next = node;
        mp->hllast = node;
    }
}

/* Floor search over every stage model with floor or water polygons. acp->nowparent / LocY / yukaattribute = the
 * highest floor in [loc.y, loc.y + len] (or the model DF_HIT_HitProc_Line hits straight down); dest = loc with
 * dest.y = the floor height. Returns the first model a floor was found on. */
StModel *DF_HIT_DecideParent(StModel *acp, float *loc, float len, float R, float *dest)
{
    PS2_FRAME(0x100);
    PS2_STACK(StModel *, res, 0xbc);
    PS2_STACK(int, nmap, 0xc0);
    PS2_STACK(float[4], dir, 0xf0);
    PS2_STACK(float[4], out, 0xe0);
    HitOldWk *w = HIT_OWK;
    int first = 0;
    w->R = R;
    res = 0;
    w->RR = R * R;
    memcpy(dest, loc, 16);
    memcpy(w->loc, loc, 16);
    w->len = len;
    float lim = w->loc[1] + len;
    w->lim = lim;
    acp->LocY = lim;
    w->loc[1] = loc[1];
    nmap = HitMapNum;
    for (int i = 0; i < nmap; i++) {
        StModel *m = HitMap[i];
        HmdHeadSt *hmd = m->hmd;
        if (!hmd->yuka && !hmd->water) continue;
        if (!hit_near_xz(w, m)) continue;
        if (!(w->loc[1] <= m->Ybottom) || w->lim < m->Ytop) continue;
        hit_mesh_to_spr(m);
        unsigned int np = hmd->p_num;
        struct_de4d68f5 *ph = hmd->php;
        do {
            HmdPrimSt *v = prim_verts(ph);
            int n = ph->pv_num - 2;
            if (v[0].g == 0xff) {
                do {
                    if (!DF_HIT_RectHitCheck_YUKA(*v[0].v_id, *v[1].v_id, *v[2].v_id)) {
                        DF_HIT_CylinderPolygon_YUKA(*v[0].v_id, *v[1].v_id, *v[2].v_id, w->loc);
                        if (w->hit && w->pos[1] < lim) {
                            float y = w->pos[1];
                            acp->LocY = y;
                            w->lim = y;
                            acp->nowparent = m;
                            lim = y;
                            acp->yukaattribute = (unsigned char)v[0].b;
                            if (!first) {
                                res = m;
                                first = 1;
                            }
                        }
                    }
                    v++;
                } while (--n);
            } else {
                v += n;
            }
            ph = (struct_de4d68f5 *)(v + 2);
        } while (--np);
    }
    if (!first) hit_parent_fallback(acp, loc, len, dir, out);
    dest[1] = lim;
    return res;
}

/* loc outside mp's bounding circle (xz, radius R): 1 = far */
int DF_HIT_DistHitCheck(StModel *mp, float *loc, float R)
{
    float dx = loc[0] - mp->mlist->sphere[0];
    if (!(dx <= R) || dx < -R) return 1;
    float dz = loc[2] - mp->mlist->sphere[2];
    if (!(dz <= R) || dz < -R) return 1;
    return !(dx * dx + dz * dz <= R * R);
}

/* HitMap / HitObj = the visible stage models (map; objects and dynamic objects) with at least 4 hit vertices */
void DF_HIT_RegistHitModel(void)
{
    HitMapNum = 0;
    int n = Array->head.mnum;
    StModel *m = Map;
    for (int i = 0; i < n; i++, m++)
        if (!(m->use & 2) && m->hmd->v_num >= 4) HitMap[HitMapNum++] = m;
    HitObjNum = 0;
    n = Array->head.onum;
    m = Obj;
    for (int i = 0; i < n; i++, m++)
        if (!(m->use & 2) && m->hmd->v_num >= 4) HitObj[HitObjNum++] = m;
    n = Array->head.dnum;
    m = Dobj;
    for (int i = 0; i < n; i++, m++)
        if (!(m->use & 2) && m->hmd->v_num >= 4) HitObj[HitObjNum++] = m;
}

/* link lists for mp[0..num): each hit model links itself, then the reachable HitMap / HitObj models */
void DF_HIT_BuildHitLink(StModel *mp, int num)
{
    int chunk = ps2_div32(0x42f6, HitMapNum + HitObjNum);
    while (num > 0) {
        int n = chunk < num ? chunk : num;
        StModel *m = mp;
        for (int i = 0; i < n; i++, m++) {
            HmdHeadSt *hmd = m->hmd;
            if (hmd->v_num < 4 || (!hmd->yuka && !hmd->kabe && !hmd->water)) continue;
            m->hlfirst = (HitModelLinkSt *)Memm_Alloc(1, 0x10, 0x10);
            m->hllast = m->hlfirst;
            m->hllast->mp = m;
            m->hllast->next = 0;
            link_hit_model(m, HitMap, HitMapNum);
            link_hit_model(m, HitObj, HitObjNum);
        }
        num -= n;
        mp += n;
    }
}

/* hit info for every stage model with floor / wall / water polygons -> HitMap (map) / HitObj (objects) */
void DF_HIT_HitInit(void)
{
    PS2_FRAME(0x60);
    int nmap = Array->head.mnum, nobj = Array->head.onum, ndobj = Array->head.dnum;
    HitEffectNum = 0;
    HitMapNum = 0;
    auto is_hit = [](StModel *m) {
        HmdHeadSt *hmd = m->hmd;
        return (hmd->yuka || hmd->kabe || hmd->water) && init_hitinfo(m);
    };
    StModel *m = Map;
    for (int i = 0; i < nmap; i++, m++)
        if (is_hit(m)) HitMap[HitMapNum++] = m;
    HitObjNum = 0;
    m = Obj;
    for (int i = 0; i < nobj; i++, m++)
        if (is_hit(m)) HitObj[HitObjNum++] = m;
    m = Dobj;
    for (int i = 0; i < ndobj; i++, m++)
        if (is_hit(m)) HitObj[HitObjNum++] = m;
}

void DF_HIT_Alloc(int nmap, int nobj)
{
    HitMap = (StModel **)Memm_Alloc(1, nmap << 2, 0x10);
    HitObj = (StModel **)Memm_Alloc(1, nobj << 2, 0x10);
}
