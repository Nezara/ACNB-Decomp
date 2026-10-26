/* src/collision/coll.cpp - collision objects (whole file): up to 256 moving spheres / lines (bullets, missiles,
 * effects) in s_mng, tested once per frame (DF_COLL_Update) against the characters, against each other and against
 * the stage boxes, reporting hits through the owner's callback func(own, hit object, result). Line objects are
 * loc + t vel spd (vel = direction); sphere objects (flag 4) have radius spd.
 * flag bits: 4 sphere, 8 shorten spd to the box hit, 0x10 test characters, 0x20 stage, 0x40 other objects,
 * 0x80 keep testing after a hit, 0x100 can be hit by objects, 0x200 objects filtered by hitchkown / hitchkrefer.
 * stat: 0 free, 1 disabled, 2 active, 3 deleted (freed after dcnt frames).
 * result: 0x5000iiii object i, 0x4000iiii AC i, 0x3000iiii stage character i, stage hits = model id | colour B << 24
 * | model type << 28. */
#include "acnb/sdk.h"
#include "hitwk.h"

static inline int coll_index(const coll_st_tag *p) { return (int)((const char *)p - (const char *)s_mng->coll) / 0x60; }

/* the other object can be hit by pColl */
static inline int coll_candidate(const coll_st_tag *pColl, const coll_st_tag *p1)
{
    return (p1->flag & 0x100) && pColl->self != p1->self && pColl != p1 &&
           (!(pColl->flag & 0x200) || p1->hitchkown == (p1->hitchkown & pColl->hitchkrefer));
}

/* report a hit at HIT_WK->info.hitpos; returns 1 when the object stops testing */
static inline int coll_report(coll_st_tag *pColl, void *hit, unsigned result)
{
    pColl->result = result;
    memcpy(pColl->hloc, HIT_WK->info.hitpos, 16);
    gcall(pColl->func, pColl->own, hit, (int)pColl->result);
    return !(pColl->flag & 0x80);
}

/* line hit: hitpos = P + N L, L shortened to the hit (tmp = the original's stack temporary for N L) */
static inline void coll_line_hitpos(float *tmp)
{
    HitSprWk *w = HIT_WK;
    w->line.L = w->line.L * w->info.tt1;
    float l = w->line.L;
    for (int k = 0; k < 3; k++) tmp[k] = w->line.N[k] * l;
    tmp[3] = w->line.N[3];
    for (int k = 0; k < 3; k++) w->info.hitpos[k] = w->line.P[k] + tmp[k];
    w->info.hitpos[3] = w->line.P[3];
}

/* AC slot i / stage character i can be hit by pColl */
static inline int coll_chr_candidate(const coll_st_tag *pColl, StModel *m, unsigned self)
{
    return pColl->self != (unsigned short)self && m && !(m->use & 2) && (!m->array || !(m->array->mflag & 0x80000));
}

static int __HitCollSphereCollProc(coll_st_tag *pColl)
{
    PS2_FRAME(0x80);
    PS2_STACK(float[4], loc, 0x70);
    HitSprWk *w = HIT_WK;
    int hit = 0;
    memcpy(w->sphere.P, pColl->loc, 16);
    w->sphere.R = pColl->spd;
    for (coll_st_tag *p1 = s_mng->head; p1; p1 = p1->next) {
        if (!coll_candidate(pColl, p1)) continue;
        memcpy(loc, p1->loc, 16);
        HitFuncSphereSphereSPR(loc, (p1->flag & 4) ? p1->spd : 2.5f);
        if (!w->info.hit) continue;
        hit = 1;
        if (coll_report(pColl, p1, (coll_index(p1) & 0xffff) | 0x50000000)) return 1;
    }
    return hit;
}

static int __HitCollLineCollProc(coll_st_tag *pColl)
{
    PS2_FRAME(0xa0);
    PS2_STACK(float[4], loc, 0x90);
    PS2_STACK(float[4], tmp, 0x80);
    HitSprWk *w = HIT_WK;
    int hit = 0;
    memcpy(w->line.P, pColl->loc, 16);
    memcpy(w->line.N, pColl->vel, 16);
    float spd = pColl->spd;
    for (int k = 0; k < 3; k++) w->line.V[k] = pColl->vel[k] * spd;
    w->line.V[3] = pColl->vel[3];
    w->line.L = spd;
    for (coll_st_tag *p1 = s_mng->head; p1; p1 = p1->next) {
        if (!coll_candidate(pColl, p1)) continue;
        memcpy(loc, p1->loc, 16);
        HitFuncLineSphereSPR(loc, (p1->flag & 4) ? p1->spd : 2.5f);
        if (!w->info.hit) continue;
        hit = 1;
        coll_line_hitpos(tmp);
        if (coll_report(pColl, p1, (coll_index(p1) & 0xffff) | 0x50000000)) return 1;
    }
    return hit;
}

static int __HitCollSphereChrProc(coll_st_tag *pColl)
{
    PS2_FRAME(0x90);
    PS2_STACK(float[4], loc, 0x80);
    HitSprWk *w = HIT_WK;
    int hit = 0;
    memcpy(w->sphere.P, pColl->loc, 16);
    w->sphere.R = pColl->spd;
    int n = NowPlayerNum;
    StModel *m = MyAC;
    for (unsigned i = 0; (int)i < n; i++, m++) {
        if (!coll_chr_candidate(pColl, m, (i & 0xfff) | 0x4000)) continue;
        memcpy(loc, m->loc, 16);
        HitFuncSphereCylinderSPR(loc, m->tall, m->R);
        if (!w->info.hit) continue;
        hit = 1;
        if (coll_report(pColl, m, (i & 0xffff) | 0x40000000)) return 1;
    }
    int cnum = StgInfo.cnum;
    m = Chr;
    for (unsigned i = 0; (int)i < cnum; i++, m++) {
        if (!coll_chr_candidate(pColl, m, (i & 0xfff) | 0x3000)) continue;
        memcpy(loc, m->loc, 16);
        HitFuncSphereCylinderSPR(loc, m->tall, m->R);
        if (!w->info.hit) continue;
        hit = 1;
        if (coll_report(pColl, m, (i & 0xffff) | 0x30000000)) return 1;
    }
    return hit;
}

static int __HitCollLineChrProc(coll_st_tag *pColl)
{
    PS2_FRAME(0xc0);
    PS2_STACK(float[4], loc, 0xb0);
    PS2_STACK(float[4], tmp_ac, 0xa0);
    PS2_STACK(float[4], tmp_chr, 0x90);
    HitSprWk *w = HIT_WK;
    int hit = 0;
    memcpy(w->line.P, pColl->loc, 16);
    memcpy(w->line.N, pColl->vel, 16);
    float spd = pColl->spd;
    for (int k = 0; k < 3; k++) w->line.V[k] = pColl->vel[k] * spd;
    w->line.V[3] = pColl->vel[3];
    w->line.L = spd;
    int n = NowPlayerNum;
    StModel *m = MyAC;
    for (unsigned i = 0; (int)i < n; i++, m++) {
        if (!coll_chr_candidate(pColl, m, (i & 0xfff) | 0x4000)) continue;
        memcpy(loc, m->loc, 16);
        HitFuncLineCylinderSPR(loc, m->tall, m->R);
        if (!w->info.hit) continue;
        hit = 1;
        coll_line_hitpos(tmp_ac);
        if (coll_report(pColl, m, (i & 0xffff) | 0x40000000)) return 1;
    }
    int cnum = StgInfo.cnum;
    m = Chr;
    for (unsigned i = 0; (int)i < cnum; i++, m++) {
        if (!coll_chr_candidate(pColl, m, (i & 0xfff) | 0x3000)) continue;
        memcpy(loc, m->loc, 16);
        HitFuncLineCylinderSPR(loc, m->tall, m->R);
        if (!w->info.hit) continue;
        hit = 1;
        coll_line_hitpos(tmp_chr);
        if (coll_report(pColl, m, (i & 0xffff) | 0x30000000)) return 1;
    }
    return hit;
}

/* unlink from the active list, append to the free list */
static int df_coll_removelist(coll_st_tag *coll)
{
    if (!coll->stat) return -4;
    if (coll->next) coll->next->prev = coll->prev;
    if (coll->prev)
        coll->prev->next = coll->next;
    else
        s_mng->head = coll->next;
    coll->next = 0;
    coll->prev = s_mng->free_tail;
    if (s_mng->free_tail)
        s_mng->free_tail->next = coll;
    else
        s_mng->free_head = coll;
    s_mng->free_tail = coll;
    coll->stat = 0;
    return 0;
}

int DF_COLL_Update(void)
{
    PS2_FRAME(0x30);
    if (!s_mng) return -1;
    coll_st_tag *p = s_mng->head;
    while (p) {
        if (p->stat == 2 && p->func) {
            if (p->flag & 0x10) {
                int r = (p->flag & 4) ? __HitCollSphereChrProc(p) : __HitCollLineChrProc(p);
                if (r && !(p->flag & 0x80)) goto next;
            }
            if (p->flag & 0x40) {
                int r = (p->flag & 4) ? __HitCollSphereCollProc(p) : __HitCollLineCollProc(p);
                if (r && !(p->flag & 0x80)) goto next;
            }
            if (p->flag & 0x20) {
                int hit = 0;
                if (p->flag & 4) {
                    if (HitBoxHitProcSphere(p->loc, p->spd, 1, 0)) hit = 1;
                } else {
                    if (HitBoxHitProcLine(p->loc, p->vel, p->spd, 1, 0)) hit = 1;
                }
                if (hit) {
                    HitSprWk *w = HIT_WK;
                    if (p->flag & 8) p->spd = w->line.L;
                    p->result = w->info.mdl_id;
                    p->result = p->result | (unsigned)w->info.color_B << 24;
                    p->result = p->result | (unsigned)w->info.mdl_type << 28;
                    memcpy(p->hloc, w->info.hitpos, 16);
                    memcpy(p->norm, w->info.hitnorm, 16);
                    gcall(p->func, p->own, (void *)w->info.pHitMdl, (int)p->result);
                }
            }
        } else if (p->stat == 3) {
            if (p->dcnt > 0) {
                p->dcnt = p->dcnt - 1;
            } else {
                coll_st_tag *nx = p->next;
                df_coll_removelist(p);
                p = nx;
                continue;
            }
        }
    next:
        p = p->next;
    }
    return 0;
}

int DF_COLL_GetVel(int id, float *vel, float *spd)
{
    if (!s_mng) return -1;
    if (id < 0 || id >= 0x100) return -3;
    coll_st_tag *c = &s_mng->coll[id];
    if (vel) memcpy(vel, c->vel, 16);
    if (spd) *spd = c->spd;
    return 0;
}

int DF_COLL_GetNorm(int id, float *norm)
{
    if (!s_mng) return -1;
    if (id < 0 || id >= 0x100) return -3;
    memcpy(norm, s_mng->coll[id].norm, 16);
    return 0;
}

int DF_COLL_GetHitLoc(int id, float *pos)
{
    if (!s_mng) return -1;
    if (id < 0 || id >= 0x100) return -3;
    memcpy(pos, s_mng->coll[id].hloc, 16);
    return 0;
}

int DF_COLL_GetPos(int id, float *pos)
{
    if (!s_mng) return -1;
    if (id < 0 || id >= 0x100) return -3;
    memcpy(pos, s_mng->coll[id].loc, 16);
    return 0;
}

int DF_COLL_SetSphr(int id, float r)
{
    if (!s_mng) return -1;
    if (id < 0 || id >= 0x100) return -3;
    s_mng->coll[id].spd = r;
    DF_COLL_SetFlag(id, 4, 0);
    return 0;
}

int DF_COLL_SetVel(int id, float *fv, float spd)
{
    if (!s_mng) return -1;
    if (id < 0 || id >= 0x100) return -3;
    if (!fv) return -5;
    memcpy(s_mng->coll[id].vel, fv, 16);
    s_mng->coll[id].spd = spd;
    return 0;
}

int DF_COLL_SetPos(int id, float *fv)
{
    if (!s_mng) return -1;
    if (id < 0 || id >= 0x100) return -3;
    if (!fv) return -5;
    memcpy(s_mng->coll[id].loc, fv, 16);
    return 0;
}

int DF_COLL_SetSelf(int id, int type, int sid)
{
    if (!s_mng) return -1;
    if (id < 0 || id >= 0x100) return -3;
    s_mng->coll[id].self = (unsigned short)((type & 0xf) << 12 | (sid & 0xfff));
    return 0;
}

/* mode 0 = set bits, 1 = clear bits, 2 = assign */
int DF_COLL_SetFlag(int id, unsigned short flag, int mode)
{
    if (!s_mng) return -1;
    if (id < 0 || id >= 0x100) return -3;
    coll_st_tag *c = &s_mng->coll[id];
    if (mode == 2)
        c->flag = flag;
    else if (mode == 1)
        c->flag = c->flag & ~flag;
    else if (mode == 0)
        c->flag = c->flag | flag;
    else
        return -5;
    return 0;
}

int DF_COLL_Delete(int id)
{
    if (!s_mng) return -1;
    if (id < 0 || id >= 0x100) return -3;
    coll_st_tag *c = &s_mng->coll[id];
    if (!c->stat) return -4;
    c->stat = 3;
    c->dcnt = 5;
    return 0;
}

/* take a free object (own and func required); en: active at once. Returns its id. */
int DF_COLL_AddList(void *own, void (*func)(void *, void *, int), int en)
{
    if (!s_mng) return -1;
    coll_st_tag *p = s_mng->free_head;
    if (!p) return -2;
    if (!own || !func) return -5;
    s_mng->free_head = p->next;
    if (!s_mng->free_head) s_mng->free_tail = 0;
    if (s_mng->head) s_mng->head->prev = p;
    p->prev = 0;
    p->next = s_mng->head;
    s_mng->head = p;
    p->own = own;
    p->func = func;
    p->stat = en ? 2 : 1;
    memcpy(p->norm, ZeroVectorF, 16);
    p->self = 0;
    p->flag = 0x70;
    return coll_index(p);
}

void DF_COLL_Create(void)
{
    s_mng = (struct_b6720aac *)Memm_Alloc(1, 0x6010, 0x10);
    ZeroMemory32b(s_mng, 0x1804);
    s_mng->free_head = s_mng->coll;
    for (int i = 0; i < 255; i++) s_mng->coll[i].next = &s_mng->coll[i + 1];
    s_mng->coll[255].next = 0;
    s_mng->free_tail = &s_mng->coll[255];
}
