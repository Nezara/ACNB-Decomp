/* src/model/light.cpp - point lights (whole file). PLight[1..127] are the live lights (slot 0 unused; on = the
 * slot number). Each frame DF_PointLightProc runs a light's type handler: Init when created, Func while frm counts
 * down, Destroy at the end. A light registers itself with up to 4 models in range (StModel.plid / plightnum, used
 * by the VU1 point-light programs): stage models (kind bit 0, via the hit boxes) and the ACs (kind bit 1).
 * Attached lights follow a position / angle given by pointer: the light sits where a 500-unit ray along the
 * rotated direction hits the stage. loc.w = 1 / range^2. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

static const float MIN_RANGE = ps2_u2f(0x358637bd);   /* 1e-6 */

/* the light's model distance test (box, then sphere; range + 18). The box test only rejects a model that is past
 * +r and below -r at once (sic: never for r >= 0), so in effect only the sphere counts. */
static inline bool ac_in_range(const float *l, StModel *m, float r, float nr, float r2)
{
    float dx = l[0] - m->loc[0];
    if (!(dx <= r) && dx < nr) return false;
    float dz = l[2] - m->loc[2];
    if (!(dz <= r) && dz < nr) return false;
    float dy = l[1] - m->loc[1];
    if (!(dy <= r) && dy < nr) return false;
    float acc = dx * dx + dy * dy;
    return acc + dz * dz <= r2;
}

/* removes light id from a model's list */
static inline void remove_plit(StModel *m, unsigned char id)
{
    int n = m->vuparam.plightnum;
    if (n <= 0) return;
    for (int j = 0; j < m->vuparam.plightnum; j++) {
        if (m->plid[j] != id) continue;
        for (; j < 3; j++) m->plid[j] = m->plid[j + 1];
        m->plid[3] = 0;
        m->vuparam.plightnum--;
        return;
    }
}

/* adds the light to the model (at most 4: the oldest drops out) */
static void _add_plit(StModel *mp, struct_d6360d99 *pp)
{
    for (int j = 0; j < 4; j++)
        if (mp->plid[j] == pp->on) return;
    if (mp->vuparam.plightnum == 4) {
        mp->plid[0] = mp->plid[1];
        mp->plid[1] = mp->plid[2];
        mp->plid[2] = mp->plid[3];
        mp->plid[3] = pp->on;
    } else {
        int j = 0;
        while (j < 4 && mp->plid[j]) j++;
        ((unsigned char *)mp->plid)[j] = pp->on;   /* (j = 4 writes past plid when the list is full but the count is not) */
        mp->vuparam.plightnum++;
    }
}

void DF_DetachPointLight(int light_id)
{
    if (light_id < 0 || light_id >= 0x80) return;
    DF_PointLightDestroy_00(&PLight[light_id]);
    ZeroMemory32b(&PLight[light_id], 0x20);
}

/* the attached light's position: ray from *loc along fwk rotated by *ang (z, x, y), 500 units into the stage */
static void attached_light_pos(struct_d6360d99 *p)
{
    const float *ang = (const float *)HOSTPTR(p->wk[1]);
    float m[4][4] __attribute__((aligned(16)));
    sceVu0CopyMatrix(m, IdentityMtx);
    sceVu0RotMatrixZ(m, m, ang[2]);
    sceVu0RotMatrixX(m, m, ang[0]);
    sceVu0RotMatrixY(m, m, ang[1]);
    float v[4];
    vu_apply(v, m, p->fwk);   /* (the original multiplies fwk + the 3 reserved qwords as a matrix; rows 1-3 unused) */
    float q = vu_rcp_len3(v);
    float dir[4] __attribute__((aligned(16))) = {v[0] * q, v[1] * q, v[2] * q, 0.0f};
    HetProcLine((float *)HOSTPTR(p->wk[0]), dir, 500.0f, p->loc, 0);
    memcpy(&p->loc[3], &p->wk[2], 4);
}

int DF_AttachPointLight(int kind, int type, float range, int frm, float r, float g, float b, float (*loc)[4],
                        float (*ang)[4], float *vec)
{
    PS2_FRAME(0x90);
    if (range <= MIN_RANGE) return -1;
    int i = 1;
    struct_d6360d99 *p = &PLight[1];
    for (; i < 0x80; i++, p++)
        if (!p->on) break;
    if (i >= 0x80) return -1;
    float inv = ps2_div(1.0f, range * range);
    p->on = (unsigned char)i;
    p->kind = (unsigned char)kind;
    p->type = (unsigned char)type;
    p->attach = 1;
    p->frm = frm;
    p->range = range;
    p->col[0] = r;
    p->col[1] = g;
    p->col[2] = b;
    p->created = 1;
    p->wk[0] = (int)PS2PTR(loc);
    p->wk[1] = (int)PS2PTR(ang);
    memcpy(&p->wk[2], &inv, 4);
    memcpy(p->fwk, vec, 16);
    attached_light_pos(p);
    return i;
}

/* unregisters the light from every stage model / AC */
static void DF_PointLightDestroy_00(struct_d6360d99 *pp)
{
    if (pp->kind & 1) {
        for (int i = 0; i < Array->head.mnum; i++) remove_plit(&Map[i], pp->on);
        for (int i = 0; i < Array->head.onum; i++) remove_plit(&Obj[i], pp->on);
        for (int i = 0; i < Array->head.dnum; i++) remove_plit(&Dobj[i], pp->on);
    }
    if (pp->kind & 2)
        for (int i = 0; i < NowPlayerNum; i++) remove_plit(&MyAC[i], pp->on);
}

/* stage models entering / leaving the range */
static void PL_Update_Map(struct_d6360d99 *pp)
{
    PS2_FRAME(0x440);   /* HitBoxGetInOutSphereModel's 10th argument goes on the stack */
    int nin = 0, nout = 0;
    StModel *in[0x80], *out[0x80];
    HitBoxGetInOutSphereModel(pp->loc, pp->range, 0x10002, 1, &nin, in, 0x80, &nout, out, 0x80);
    for (int i = 0; i < nin; i++) _add_plit(in[i], pp);
    for (int i = 0; i < nout; i++) remove_plit(out[i], pp->on);
}

static void PL_Update_AC(struct_d6360d99 *pp)
{
    float r = 18.0f + pp->range;
    if (!(0 < NowPlayerNum)) return;
    float r2 = r * r, nr = -r;
    for (int i = 0; i < NowPlayerNum; i++) {
        StModel *m = &MyAC[i];
        if (ac_in_range(pp->loc, m, r, nr, r2)) _add_plit(m, pp);
        else remove_plit(m, pp->on);
    }
}

/* type 1: follows its attachment and re-registers every frame (never expires by itself) */
static void DF_PointLightFunc_01(struct_d6360d99 *pp)
{
    PS2_FRAME(0x70);
    if (pp->attach) attached_light_pos(pp);
    if (pp->kind & 1) PL_Update_Map(pp);
    if (pp->kind & 2) PL_Update_AC(pp);
    pp->frm = 2;
}

/* type 0: fades (the falloff 1 / range^2 grows by 10% a frame) */
static void DF_PointLightFunc_00(struct_d6360d99 *pp) { pp->loc[3] = pp->loc[3] * ps2_u2f(0x3f8ccccd); }

static void DF_PointLightInit_00(struct_d6360d99 *pp)
{
    if (pp->kind & 1) {
        int n = 0;
        StModel *list[0x80];
        HitBoxGetInSphereModel(pp->loc, pp->range, 0x10002, 1, &n, list, 0x80);
        for (int i = 0; i < n; i++) _add_plit(list[i], pp);
    }
    if (pp->kind & 2) {
        float l[4];
        memcpy(l, pp->loc, 16);
        int nac = NowPlayerNum;
        if (nac > 0) {
            float r = 18.0f + pp->range;
            float r2 = r * r, nr = -r;
            for (int i = 0; i < nac; i++)
                if (ac_in_range(l, &MyAC[i], r, nr, r2)) _add_plit(&MyAC[i], pp);
        }
    }
}

void DF_PointLightProc(void)
{
    PS2_FRAME(0x40);
    int n = 0;
    for (int i = 1; i < 0x80; i++) {
        struct_d6360d99 *p = &PLight[i];
        if (!p->on) continue;
        n++;
        if (p->created) {
            gcall(DF_PointLightInit[p->type], p);
            p->created = 0;
        } else if (--p->frm < 1) {
            gcall(DF_PointLightDestroy[p->type], p);
            p->on = 0;
        } else {
            gcall(DF_PointLightFunc[p->type], p);
        }
    }
    PLightNum = n;
}

/* a free-standing light at loc; returns its id or -1 */
int DF_PutPointLight(int kind, int type, float *loc, float range, int frm, float r, float g, float b)
{
    if (range <= MIN_RANGE) return -1;
    int i = 1;
    struct_d6360d99 *p = &PLight[1];
    for (; i < 0x80; i++, p++)
        if (!p->on) break;
    if (i >= 0x80) return -1;
    p->on = (unsigned char)i;
    p->kind = (unsigned char)kind;
    p->type = (unsigned char)type;
    p->frm = frm;
    p->range = range;
    memcpy(p->loc, loc, 16);
    p->loc[3] = ps2_div(1.0f, range * range);
    p->col[0] = r;
    p->col[1] = g;
    p->col[2] = b;
    p->created = 1;
    return i;
}

void PLIT_Reset(void) { ZeroMemory32b(PLight, 0x1000); }

#ifdef ACNB_SOURCEPORT
/* the original's initialized tables (generated by scripts/sp_datagen.py from the PS2 image) */
void (*DF_PointLightDestroy[2])(struct struct_d6360d99 *) = {   /* main 0x27afa8 */
    DF_PointLightDestroy_00, DF_PointLightDestroy_00
};
void (*DF_PointLightFunc[2])(struct struct_d6360d99 *) = {   /* main 0x27afa0 */
    DF_PointLightFunc_00, DF_PointLightFunc_01
};
void (*DF_PointLightInit[2])(struct struct_d6360d99 *) = {   /* main 0x27af98 */
    DF_PointLightInit_00, DF_PointLightInit_00
};
#endif
