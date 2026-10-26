/* src/model/anim.cpp - hierarchical model animation (whole file).
 * A model's StXtod holds up to 94 animations ("tod"): head[] = file header (frame count), animtable[] = one
 * StMixAnimationTable per object (joint) with its key list. Two players run over it:
 *   - per-object animation (__UpdateTod, Anim_ChangeAnim*, Anim_SetAnim*): each ObjectSt has its own animation id,
 *     frame, loop flag and a blend ("hokan") from its previous pose; ACs and characters use this one;
 *   - whole-model "mi2" animation (Anim_AnimMi2TodUpdate*): one frame counter in the StXtod for all objects
 *     (event / stage props), N variants also move the model root by object 0.
 * Keys are cubic Hermite splines (position and angle, each with in / out tangents), evaluated as the VU0 ACC chain
 * of the original inline asm; the stored vector's w is the 4th basis weight (t^3 - t^2), as in the original. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

static const float PI_F = ps2_u2f(0x40490fdb), TWO_PI_F = ps2_u2f(0x40c90fdb);

/* Hermite basis (h00, h01, h10, h11) at t, in the original operation order. */
static inline void hermite(float *h, float t)
{
    float t2 = t * t, t3 = t2 * t;
    h[0] = 1.0f + (2.0f * t3 - 3.0f * t2);
    h[1] = -2.0f * t3 + 3.0f * t2;
    h[2] = t + (t3 - 2.0f * t2);
    h[3] = t3 - t2;
}

/* out.xyz = p0*h00 + p1*h01 + dp0*h10 + dp1*h11 (vmulax / vmadday / vmaddaz / vmaddw.xyz on the basis vector);
 * out.w = h11 (the .xyz-masked result register keeps the basis vector's w, and sqc2 stores all four). */
static inline void spline(float *out, const float *p0, const float *p1, const float *dp0, const float *dp1, const float *h)
{
    float r[4];
    for (int i = 0; i < 3; i++) {
        float a = p0[i] * h[0];
        a = a + p1[i] * h[1];
        a = a + dp0[i] * h[2];
        r[i] = a + dp1[i] * h[3];
    }
    r[3] = h[3];
    memcpy(out, r, 16);
}

/* to [-pi, pi] by whole turns */
static inline void wrap_pi(float *a)
{
    while (!(*a <= PI_F)) *a = *a - TWO_PI_F;
    while (*a < -PI_F) *a = *a + TWO_PI_F;
}

/* to [-pi, pi) with one truncating division */
static inline float norm_angle(float a)
{
    float s = PI_F + a;
    float n = (float)ps2_ftoi(s / TWO_PI_F);
    return (s - TWO_PI_F * n) - PI_F;
}

void Anim_CubicSpline(float *outv, float *p0, float *p1, float *dp0, float *dp1, float t)
{
    float h[4];
    hermite(h, t);
    spline(outv, p0, p1, dp0, dp1, h);
}

void Anim_SetSfx(StModel *mp, int sfx)
{
    if (sfx) mp->animation |= 1;
    else mp->animation &= 0xfe;
}

/* Position of an object at frame (key search starts at keyidx, as __UpdateTod's). */
static void __GetFramePos(float *pos, StMixAnimationTable *animp, int frame, int keyidx)
{
    if (frame < animp->tod_data[keyidx].key) keyidx = 0;
    int nk = animp->nkey_tod;
    struct_3c2ab559 *k0 = &animp->tod_data[keyidx];
    while (keyidx < nk - 1 && !(frame < k0[1].key)) {
        keyidx++;
        k0++;
    }
    struct_3c2ab559 *k1 = k0;
    float t = 1.0f;
    if (keyidx + 1 < nk) {
        t = ps2_div((float)(frame - k0->key), (float)k0->nframe);
        k1 = k0 + 1;
    }
    float h[4];
    hermite(h, t);
    spline(pos, k0->position, k1->position, k0->pos_outtan, k1->pos_intan, h);
}

/* Root object movement over the next frame (w = the first position's w). */
void Anim_GetMoveVector(StModel *modelp, float *vel)
{
    ObjectSt *o = modelp->op;
    if (!(o->todid < modelp->tod->numoftod)) return;
    StMixAnimationTable *animp = modelp->tod->animtable[o->todid];
    vel[0] = 0.0f;
    vel[1] = 0.0f;
    vel[2] = 0.0f;
    vel[3] = 1.0f;
    if (!animp->nkey_tod) return;
    float next[4] __attribute__((aligned(16))), cur[4] __attribute__((aligned(16)));
    unsigned short frame = o->frame;
    __GetFramePos(next, animp, frame + 1, (int)o->now_key);
    __GetFramePos(cur, animp, frame, (int)o->now_key);
    float r[4] = {next[0] - cur[0], next[1] - cur[1], next[2] - cur[2], next[3]};
    memcpy(vel, r, 16);
}

/* 1 when no object of the group still plays animid (frames left or blending in). */
int Anim_IsEndGroupAnim(StModel *modelp, int groupno, int animid)
{
    if (!modelp || animid < 0 || !(animid < modelp->tod->numoftod)) return 1;
    ObjectSt *o = modelp->op;
    unsigned int mask = 1u << (groupno & 31);
    for (int i = 0; i < modelp->nobj; i++, o++)
        if ((o->group & mask) && !o->noanim && o->todid == animid &&
            (o->frame < o->nframe || o->hokanframe < o->hokantime))
            return 0;
    return 1;
}

int Anim_IsEndGroup(StModel *modelp, int groupno)
{
    if (groupno < 0 || groupno >= 8) return 1;
    return Anim_IsEndGroupAnim(modelp, groupno, modelp->grptodid[groupno]);
}

int Anim_IsEndAnim(StModel *modelp, int anmid) { return Anim_IsEndGroupAnim(modelp, 0, anmid); }

int Anim_IsEnd(StModel *modelp) { return Anim_IsEndGroupAnim(modelp, 0, modelp->grptodid[0]); }

/* mi2 animation played backwards has ended (looping: back at the last frame) */
int Anim_IsEndTopReverse(StModel *pModel)
{
    StXtod *tod = pModel->tod;
    if (pModel->animloop) return tod->nowframe == tod->head[tod->nowtodid]->frame;
    return tod->nowframe == 0;
}

/* mi2 animation has ended (looping: wrapped to frame 0) */
int Anim_IsEndTod(StModel *mp)
{
    StXtod *tod = mp->tod;
    if (mp->animloop) return tod->nowframe == 0;
    return !(tod->nowframe < tod->head[tod->nowtodid]->frame);
}

/* wrmat = parent * (RotX, RotZ, RotY, Trans of the object), angles normalised first */
static void obj_world_matrix(ObjectSt *objp, const float (*parentmat)[4])
{
    sceVu0RotMatrixX(*objp->wrmatp, IdentityMtx, norm_angle(objp->angle[0]));
    sceVu0RotMatrixZ(*objp->wrmatp, *objp->wrmatp, norm_angle(objp->angle[2]));
    sceVu0RotMatrixY(*objp->wrmatp, *objp->wrmatp, norm_angle(objp->angle[1]));
    sceVu0TransMatrix(*objp->wrmatp, *objp->wrmatp, objp->position);
    vu_mul_matrix(*objp->wrmatp, parentmat, *objp->wrmatp);
}

/* Object tree walk: siblings share the parent matrix, children get this object's. flag bit 0 = not attached to the
 * parent (identity instead; the multiply still runs). */
void Anim_UpdateChildObjMatrix(ObjectSt *objp, float (*parentmat)[4])
{
    obj_world_matrix(objp, (objp->flag & 1) ? IdentityMtx : parentmat);
    if (objp->sibling) Anim_UpdateChildObjMatrix(objp->sibling, parentmat);
    if (objp->child) Anim_UpdateChildObjMatrix(objp->child, *objp->wrmatp);
}

void Anim_UpdateMyObjMatrix(ObjectSt *objp, void *parentmat)
{
    obj_world_matrix(objp, (const float (*)[4])parentmat);
}

/* Per-object animation step for the objects in groupbit: pose at the current frame, blended into the object's pose
 * by the hokan rate (1 = take the key pose), then frame += incframe (loop or hold at the end). */
static void __UpdateTod(StModel *modelp, int groupbit)
{
    StXtod *tod = modelp->tod;
    ObjectSt *o = modelp->op;
    for (int i = 0; i < modelp->nobj; i++, o++) {
        if (o->noanim || !(o->group & groupbit) || o->todid == 0xff) continue;
        StMixAnimationTable *at = &tod->animtable[o->todid][i];
        if (!at->tod_data) continue;
        float ang[4], pos[4];
        if (!at->nkey_tod) {
            memcpy(ang, at->init_rot, 16);
            memcpy(pos, at->init_trans, 16);
        } else {
            int k = (int)o->now_key, nk = at->nkey_tod;
            if (!(k < nk) || o->frame < at->tod_data[k].key) k = 0;
            struct_3c2ab559 *k0 = &at->tod_data[k];
            while (k < nk - 1 && !(o->frame < k0[1].key)) {
                k++;
                k0++;
            }
            o->now_key = (unsigned int)k;
            struct_3c2ab559 *k1 = k0;
            float t = 1.0f;
            if (k + 1 < nk) {
                k1 = k0 + 1;
                t = ps2_div((float)(int)(o->frame - k0->key), (float)k0->nframe);
            }
            float h[4];
            hermite(h, t);
            spline(ang, k0->angle, k1->angle, k0->ang_outtan, k1->ang_intan, h);
            spline(pos, k0->position, k1->position, k0->pos_outtan, k1->pos_intan, h);
        }
        float rate;
        if (o->hokanframe < o->hokantime) {
            o->hokanframe = o->hokanframe + 1;
            if (o->hokantype == 1) {   /* the remaining distance in equal steps */
                float d = (float)(o->hokantime - (o->hokanframe - 1));
                rate = d == 0.0f ? 1.0f : 1.0f / d;
            } else if (o->hokantype == 2) {   /* ease in / out (the share of the rest a cubic step covers) */
                float T = (float)o->hokantime;
                float T3 = T * (T * T);
                float f = (float)(o->hokanframe - 1);
                float f2 = f * f;
                float acc = T3 + 2.0f * (f2 * f);
                float num = 3.0f * T + (-6.0f * f2 - (6.0f * f) * (1.0f - T));
                float den = acc - (3.0f * T) * f2;
                rate = den == 0.0f ? 1.0f : (num - 2.0f) / den;
            } else {   /* linear */
                rate = ps2_div((float)o->hokanframe, (float)o->hokantime);
            }
        } else {
            o->hokanframe = o->hokantime;
            rate = 1.0f;
        }
        o->hokanrate = rate;
        for (int c = 0; c < 3; c++) wrap_pi(&ang[c]);
        for (int c = 0; c < 3; c++) {
            float d = ang[c] - o->angle[c];
            d = d * rate;
            o->angle[c] = o->angle[c] + d;
        }
        for (int c = 0; c < 3; c++) wrap_pi(&o->angle[c]);
        for (int c = 0; c < 3; c++) o->position[c] = o->position[c] + rate * (pos[c] - o->position[c]);
        o->frame = (unsigned short)(o->frame + o->incframe);
        unsigned short nf = o->nframe;
        if (!(o->frame < nf)) {
            if (nf == 0 || o->animloop) {
                o->frame = 0;
                o->now_key = 0;
            } else {
                o->frame = nf;
            }
        }
    }
}

void Anim_UpdateGroup(StModel *mp, int groupbit) { __UpdateTod(mp, groupbit); }

void Anim_Update(StModel *mp) { __UpdateTod(mp, -1); }

/* Interpolates mi2 key k0 -> k0 + 1 at the model's frame: position, then angle (wrapped). */
static void mi2_pose_n(ObjectSt *o, struct_3c2ab559 *k0, unsigned short nowframe)
{
    struct_3c2ab559 *k1 = k0 + 1;
    float t = (float)(int)(nowframe - k0->key) / (float)k0->nframe;
    if (!k0->nframe) t = 1.0f;
    float h[4];
    hermite(h, t);
    spline(o->position, k0->position, k1->position, k0->pos_outtan, k1->pos_intan, h);
    spline(o->angle, k0->angle, k1->angle, k0->ang_outtan, k1->ang_intan, h);
    for (int c = 0; c < 3; c++) wrap_pi(&o->angle[c]);
}

/* N variants: object 0's pose moves the model itself (matrix2 = model local matrix * object 0 matrix; loc / ang /
 * bounding sphere / Ytop / Ybottom follow), object 0 is reset to identity and the object matrices are rebuilt. */
static void mi2_move_root(StModel *mp)
{
    ObjectSt *op = mp->op;
    float m[4][4] __attribute__((aligned(16)));
    sceVu0CopyMatrix(m, IdentityMtx);
    sceVu0RotMatrixZ(m, m, op->angle[2]);
    sceVu0RotMatrixX(m, m, op->angle[0]);
    sceVu0RotMatrixY(m, m, op->angle[1]);
    sceVu0TransMatrix(m, m, op->position);
    ArraySt *a = mp->array;
    DF_MakeModelLocalMatrixZXY_Model(mp, a->loc, a->ang, a->scale);
    vu_mul_matrix(mp->matrix2, mp->matrix2, m);
    memcpy(mp->loc, mp->matrix2[3], 16);
    float ang[4] = {mp->array->ang[0] + op->angle[0], mp->array->ang[1] + op->angle[1],
                    mp->array->ang[2] + op->angle[2], mp->array->ang[3]};
    memcpy(mp->ang, ang, 16);
    float old[4];
    memcpy(old, mp->mlist->sphere, 16);
    _map_model_list_st *ml = mp->mlist;
    vu_apply(ml->sphere, mp->matrix2, mp->localcenter);
    ml->sphere[3] = old[3];
    float dy = mp->mlist->sphere[1] - old[1];
    mp->Ytop = mp->Ytop + dy;
    mp->Ybottom = mp->Ybottom + dy;
    op->position[0] = 0.0f;
    op->position[1] = 0.0f;
    op->position[2] = 0.0f;
    op->position[3] = 1.0f;
    op->angle[0] = 0.0f;
    op->angle[1] = 0.0f;
    op->angle[2] = 0.0f;
    op->angle[3] = 1.0f;
    sceVu0CopyMatrix(*mp->op->wrmatp, mp->matrix2);
    for (int i = 1; i < mp->nobj; i++) DF_MakeObjectLocalMatrix(*mp->op[i].wrmatp, &mp->op[i]);
    for (int i = 0; i < mp->nobj; i++) DF_MakeParentChain(&mp->op[i]);
}

void Anim_AnimMi2TodUpdateReverseN(StModel *mp)
{
    StXtod *tod = mp->tod;
    struct_e6319a72 *head = tod->head[tod->nowtodid];
    StMixAnimationTable *at = tod->animtable[tod->nowtodid];
    for (int i = 0; i < mp->nobj; i++, at++) {
        ObjectSt *o = &mp->op[i];
        if (o->noanim || !at->nkey_tod) continue;
        o->now_key = at->nkey_tod - 1u;
        struct_3c2ab559 *k = &at->tod_data[at->nkey_tod - 1];
        while (!(k->key < tod->nowframe)) {   /* last key before the frame */
            k--;
            o->now_key--;
        }
        mi2_pose_n(o, k, tod->nowframe);
    }
    mi2_move_root(mp);
    if (tod->nowframe) tod->nowframe--;
    if (!tod->nowframe) tod->nowframe = mp->animloop ? (unsigned short)head->frame : 0;
}

void Anim_AnimMi2TodUpdateN(StModel *mp)
{
    StXtod *tod = mp->tod;
    struct_e6319a72 *head = tod->head[tod->nowtodid];
    StMixAnimationTable *at = tod->animtable[tod->nowtodid];
    for (int i = 0; i < mp->nobj; i++, at++) {
        ObjectSt *o = &mp->op[i];
        if (o->noanim || !at->nkey_tod) continue;
        o->now_key = 0;
        struct_3c2ab559 *k = at->tod_data;
        while (!(tod->nowframe < k->key)) {   /* first key after the frame */
            k++;
            o->now_key++;
        }
        mi2_pose_n(o, k - 1, tod->nowframe);
    }
    mi2_move_root(mp);
    tod->nowframe++;
    if (!(tod->nowframe < head->frame)) tod->nowframe = mp->animloop ? 0 : (unsigned short)head->frame;
}

/* Non-N variants: key search from the object's last key; a key with scale.x > 1 on object >= 2 of a non-tp2 object
 * fires spell (scale.x - 1) * 10000 at the object (models with the sfx flag). Object matrices are then rebuilt and
 * put under the model matrix. */
static void mi2_pose(StModel *mp, int i, StMixAnimationTable *at, struct_e6319a72 *head)
{
    StXtod *tod = mp->tod;
    ObjectSt *o = &mp->op[i];
    if (o->noanim) return;
    if (!tod->nowframe) o->now_key = 0;
    int nk = at->nkey_tod;
    if (!nk) return;
    int k = (int)o->now_key;
    struct_3c2ab559 *kp = &at->tod_data[k];
    for (; k < nk; k++, kp++) {
        unsigned short f = tod->nowframe;
        if (f < kp->key || (f == head->frame && f == kp->key)) break;
    }
    if (k == nk) {
        o->now_key = 0;
        return;
    }
    if (!k) return;
    o->now_key = (unsigned int)(k - 1);
    struct_3c2ab559 *k0 = kp - 1;
    if ((mp->animation & 1) && at->tp2id == 0xff && i >= 2 && tod->nowframe && tod->nowframe == k0->key &&
        !(k0->scale[0] <= 1.0f))
        DF_SPELL2_AppearSpell((short)ps2_ftoi(0.5f + 10000.0f * (k0->scale[0] - 1.0f)), 0, 0, 0, 0,
                              (*mp->op[i].wrmatp)[3], ZeroVectorF, 0, 0, 0, 0, 0);
    float t = ps2_div((float)(int)(tod->nowframe - k0->key), (float)k0->nframe);
    float h[4];
    hermite(h, t);
    spline(o->angle, k0->angle, kp->angle, k0->ang_outtan, kp->ang_intan, h);
    spline(o->position, k0->position, kp->position, k0->pos_outtan, kp->pos_intan, h);
}

static void mi2_objects_to_model(StModel *mp)
{
    for (int i = 0; i < mp->nobj; i++) DF_MakeObjectLocalMatrix(*mp->op[i].wrmatp, &mp->op[i]);
    for (int i = 0; i < mp->nobj; i++) DF_MakeParentChain(&mp->op[i]);
    for (int i = 0; i < mp->nobj; i++) vu_mul_matrix(*mp->op[i].wrmatp, mp->matrix2, *mp->op[i].wrmatp);
}

void Anim_AnimMi2TodUpdateReverse(StModel *pModel)
{
    PS2_FRAME(0xe0);   /* DF_SPELL2_AppearSpell's stack arguments */
    StXtod *tod = pModel->tod;
    struct_e6319a72 *head = tod->head[tod->nowtodid];
    StMixAnimationTable *at = tod->animtable[tod->nowtodid];
    for (int i = 0; i < pModel->nobj; i++, at++) mi2_pose(pModel, i, at, head);
    mi2_objects_to_model(pModel);
    tod->nowframe--;
    if (!tod->nowframe) tod->nowframe = pModel->animloop ? (unsigned short)head->frame : 0;
}

void Anim_AnimMi2TodUpdate(StModel *mp)
{
    PS2_FRAME(0xe0);   /* DF_SPELL2_AppearSpell's stack arguments */
    StXtod *tod = mp->tod;
    struct_e6319a72 *head = tod->head[tod->nowtodid];
    StMixAnimationTable *at = tod->animtable[tod->nowtodid];
    for (int i = 0; i < mp->nobj; i++, at++) mi2_pose(mp, i, at, head);
    mi2_objects_to_model(mp);
    tod->nowframe++;
    if (!(tod->nowframe < head->frame) && mp->animloop) tod->nowframe = 0;
}

/* Switches the objects of groupbit to animid. startframe < 0 keeps the relative position in the old animation;
 * hokantime < 0 = blend over the whole (first object's) animation. Returns 0 while some object still blends. */
static int __ChangeAnimGroupXframeSpdset(StModel *modelp, int groupbit, int animid, int startframe, int incframe,
                                         int hokantime, int loop)
{
    int ret = 1;
    if (!modelp) return 0;
    if (animid < 0 || !(animid < modelp->tod->numoftod)) return ret;
    for (int i = 0, b = 1; i < 8; i++, b <<= 1)
        if (groupbit & b) modelp->grptodid[i] = (unsigned char)animid;
    ObjectSt *o = modelp->op;
    for (int i = 0; i < modelp->nobj; i++, o++) {
        if (o->noanim || !(o->group & groupbit)) continue;
        if (o->todid != animid) {
            if (startframe < 0) {
                float f = (float)modelp->tod->head[animid]->frame * ps2_div((float)o->frame, (float)o->nframe);
                const float two31 = 2147483648.0f;   /* float -> unsigned as MWCC converts it */
                o->frame = (unsigned short)(two31 <= f ? ps2_ftoi(f - two31) | (int)0x80000000 : ps2_ftoi(f));
            } else {
                o->frame = (unsigned short)startframe;
            }
            o->nframe = (unsigned short)modelp->tod->head[animid]->frame;
            if (!o->nframe) o->nframe = 1;
            if (!(o->frame < o->nframe)) o->frame = loop ? (unsigned short)(o->frame % o->nframe) : o->nframe;
            o->todid = (unsigned char)animid;
            o->hokanframe = 0;
            o->hokanrate = 0.0f;
        }
        if (hokantime < 0) hokantime = o->nframe;
        o->hokantime = hokantime;
        o->animloop = (unsigned char)loop;
        o->incframe = (unsigned char)incframe;
        if (o->hokanframe < hokantime) ret = 0;
    }
    return ret;
}

int Anim_ChangeAnimGroupXframeStop(StModel *mp, int groupbit, int animid, int startframe, int hokantime, int loop)
{
    return __ChangeAnimGroupXframeSpdset(mp, groupbit, animid, startframe, 0, hokantime, loop);
}

int Anim_ChangeAnimGroupXframe(StModel *mp, int groupbit, int animid, int startframe, int hokantime, int loop)
{
    return __ChangeAnimGroupXframeSpdset(mp, groupbit, animid, startframe, 1, hokantime, loop);
}

int Anim_ChangeAnimGroup(StModel *mp, int groupbit, int animid, int hokantime, int loop)
{
    return __ChangeAnimGroupXframeSpdset(mp, groupbit, animid, 0, 1, hokantime, loop);
}

int Anim_ChangeAnimSameFrame(StModel *modelp, int animid, int hokantime, int loop)
{
    return __ChangeAnimGroupXframeSpdset(modelp, 1, animid, -1, 1, hokantime, loop);
}

int Anim_ChangeAnimLastFrame(StModel *mp, int animid, int hokantime)
{
    if (!mp || animid < 0 || !(animid < mp->tod->numoftod)) return 0;
    return __ChangeAnimGroupXframeSpdset(mp, 1, animid, (int)mp->tod->head[animid]->frame, 0, hokantime, 0);
}

int Anim_ChangeAnimNoInc(StModel *mp, int animid, int hokantime, int loop)
{
    return __ChangeAnimGroupXframeSpdset(mp, 1, animid, 0, 0, hokantime, loop);
}

int Anim_ChangeAnim(StModel *mp, int animid, int hokantime, int loop)
{
    return __ChangeAnimGroupXframeSpdset(mp, 1, animid, 0, 1, hokantime, loop);
}

/* restart animation from frame 0, no blend */
static inline void set_obj_anim(ObjectSt *o, StModel *m, int animid, int loop)
{
    o->frame = 0;
    o->nframe = (unsigned short)m->tod->head[animid]->frame;
    if (!o->nframe) o->nframe = 1;
    o->todid = (unsigned char)animid;
    o->hokanframe = 0;
    o->hokanrate = 0.0f;
    o->hokantime = 0;
    o->animloop = (unsigned char)loop;
    o->incframe = 1;
}

int Anim_SetAnimGroup(StModel *pModel, int groupID, int animID, int loop)
{
    if (!pModel || !pModel->tod || !pModel->op || animID < 0 || !(animID < pModel->tod->numoftod)) return 0;
    pModel->grptodid[groupID] = (unsigned char)animID;
    unsigned int mask = 1u << (groupID & 31);
    ObjectSt *o = pModel->op;
    for (int i = 0; i < pModel->nobj; i++, o++)
        if (!o->noanim && (o->group & mask)) set_obj_anim(o, pModel, animID, loop);
    return 1;
}

int Anim_SetAnim(StModel *modelp, int animid, int loop)
{
    if (!modelp) return 0;
    if (animid < 0 || !(animid < modelp->tod->numoftod)) return 0;
    modelp->grptodid[0] = (unsigned char)animid;   /* (group bit 1 only) */
    ObjectSt *o = modelp->op;
    for (int i = 0; i < modelp->nobj; i++, o++)
        if (!o->noanim) set_obj_anim(o, modelp, animid, loop);
    return 1;
}

void Anim_SetLoop(StModel *mp, int loop) { mp->animloop = loop != 0; }

/* selects mi2 animation id (looping, from frame 0) */
void Anim_SetAnimId(StModel *pModel, int id)
{
    pModel->tod->nowtodid = (unsigned char)id;
    pModel->tod->nowframe = 0;
    for (int i = 0; i < pModel->nobj; i++) pModel->op[i].now_key = 0;
    pModel->animloop = 1;
}

void Anim_MakeGroups(StModel *mp, int objid_min, int objid_max, int groupbit)
{
    for (int i = objid_min; i <= objid_max; i++) mp->op[i].group = (unsigned char)groupbit;
}

void Anim_MakeGroup(StModel *mp, int objid, int groupbit) { mp->op[objid].group = (unsigned char)groupbit; }

/* Mix file: count, a 16-byte entry per animation, then the mi2 animations back to back. */
void *Anim_ReadMixPointer(char *bufp, StModel *mp, int mode)
{
    char *p = bufp + *(int *)bufp * 0x10 + 0x10;
    if (!mp->tod) mp->tod = (StXtod *)Memm_Alloc(1, 0x300, 0x10);
    memset(mp->tod, 0, 0x300);
    for (unsigned int i = 0; i < *(unsigned int *)bufp; i++) p = (char *)Anim_ReadMi2Pointer(p, mp, mode);
    return p;
}

/* Registers one mi2 animation (header, object tables, keys) as the model's next tod; mode != 0 relocates the key
 * offsets. Objects get their tree links and the first key's pose; make-joints are mapped from tp2 ids to objects. */
void *Anim_ReadMi2Pointer(char *bufp, StModel *mp, int mode)
{
    StXtod *tod = mp->tod;
    tod->nowtodid = tod->numoftod;
    tod->numoftod++;
    tod->nowframe = 0;
    mp->kind |= 1;
    tod->head[tod->nowtodid] = (struct_e6319a72 *)bufp;
    mp->nobj = (unsigned short)tod->head[tod->nowtodid]->nobj;
    StMixAnimationTable *tbl = (StMixAnimationTable *)(bufp + 0x10);
    if (!mp->op) mp->op = MDL_AllocObject(mp->nobj);
    tod->animtable[tod->nowtodid] = tbl;
    char *end = (char *)(tbl + mp->nobj);
    for (int i = 0; i < mp->nobj; i++) {
        StMixAnimationTable *at = &tbl[i];
        ObjectSt *o = &mp->op[i];
        o->now_key = 0;
        if (mode && at->tod_data) at->tod_data = (struct_3c2ab559 *)(bufp + (uint32_t)(uintptr_t)at->tod_data);
        /* child / sibling hold file offsets of the tables (from bufp + 0x10) */
        o->child = at->child ? mp->op + ((uint32_t)(uintptr_t)at->child - 0x10u) / 0x30u : 0;
        mp->op[i].sibling = at->sibling ? mp->op + ((uint32_t)(uintptr_t)at->sibling - 0x10u) / 0x30u : 0;
        const struct_3c2ab559 *k = at->tod_data ? at->tod_data : (const struct_3c2ab559 *)PS2_NULL_MEM();
        memcpy(mp->op[i].position, k->position, 16);
        memcpy(mp->op[i].angle, k->angle, 16);
        mp->op[i].objid = at->objid;
        end += at->nkey_tod * 0x80;
    }
    if (mp->kind & 0x4000) {
        for (int j = 0; j < mp->nmobj; j++) {
            struct_263a14bd *mj = &mp->makejointp[j];
            if (mj->changed) continue;
            for (int i = 0; i < mp->nobj; i++)
                if ((unsigned int)tbl[i].tp2id != 0xffffu && tbl[i].tp2id == (unsigned int)mj->objid) {
                    mj->objid = tbl[i].objid;
                    mp->makejointp[j].changed = 1;
                    break;
                }
        }
    }
    return end;
}
