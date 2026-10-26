/* src/sfx/magic.c (whole file) - "magic2" particle/effect system: up to 511 live effects in Mag2St[1..511] (slot 0
 * unused), each driven by a _magic2_data_st record (MagicData2 or a combined sfx file), with parent/child chains
 * (oya), key-framed scale/colour (Animkey2/3/4 + erp_* easing), optional point lights, texture animation and motion
 * blur copies. DF_MAGIC2_Proc runs once per frame; DF_MAGIC2_Draw builds the VIF/GIF packet of one effect.
 * Effects are spawned through "duplicates" (s_Duplicate, a linked list from duplicate_top): DF_MAGIC2_Appear*Ptr
 * queue one, DF_MAGIC2_MakeMagic emits its effects every interval frames. Boost effects (flag 0x100, AppearBoost)
 * stay allocated and ping-pong their key frame range while boost_sw is on. StillLiveID counts the live users of an
 * effect id (effects and duplicates sharing a parent's id), so DF_MAGIC2_Kill_ID can remove a whole family.
 *
 * The original uses scratchpad 0x70001000.. (matrices, angles) and 0x70003000 / 0x70000000 as temporaries; those
 * stores are kept (SPR_F) so the scratchpad matches the interpreter exactly.
 * Quirks: DF_MAGIC2_MainBoost spins ang[2] twice per frame when ang_opt == 1; DF_MAGIC2_AppearMagic_SPL2(Alpha)Ptr
 * allocate the duplicate before the 0x400 / 0x8000 random rejection, which then leaves a zeroed entry (mdp 0)
 * linked in the list (MakeMagic would read the effect data from address 0; no Versus caller passes those flags). */
#include "acnb/sdk.h"
#include "acnb/vu0m.h"
#include "acnb/nodwarf.h"

#define SPR_F(off) ((float *)PS2_SPR(off))
#define SPR_M(off) ((float (*)[4])PS2_SPR(off))

static float erp_call(int k, float t)
{
    return gcall(erp_tab[k], t);
}

static void anim_call(_magic2_st *mp)
{
    gcall(Magic2AnimFunc[(int)mp->anim_func], mp);
}

/* ---------------------------------------------------------------- easing */

float erp_seo(float itp) { return itp * itp * itp; }
float erp_sei(float itp) { return itp * (itp * (itp - 3.0f) + 3.0f); }
float erp_eo(float itp) { return itp * itp; }
float erp_ei(float itp) { return itp * (2.0f - itp); }
float erp_linear(float itp) { return itp; }

/* ---------------------------------------------------------------- key animation */

/* Piecewise key interpolation: keys at frames f[0..nseg] with values v[] (percent), segment k eased by
 * erp_tab[e[k + 1]]. t = cnt % (end + 1). The segment search advances at most nseg times (as the original). */
static float key_value(int t, int nseg, const short *f, const float *v, const signed char *e)
{
    int k = 0, n = nseg - 1;
    do {
        if (t <= f[k + 1]) break;
        k++;
    } while (n-- != 0);
    float x = ps2_div((float)(t - f[k]), (float)(f[k + 1] - f[k]));
    float r = erp_call(e[k + 1], x);
    return v[k] + (v[k + 1] - v[k]) * r;
}

static int key_end_frame(const _magic2_data_st *d) { return d->frame ? d->frame : 2; }

/* 3 keys (key 0, 1, 3) per channel; scale = scale0 * v / 100, rgba = rgba0 * v / 100 */
void DF_MAGIC2_Animkey3(_magic2_st *mp)
{
    PS2_FRAME(0xd0);
    if (!mp->cnt) return;
    int end = key_end_frame(mp->mdp);
    int t = ps2_mod32(mp->cnt, end + 1);
    short f[5] = {0, 0, (short)end, 0, 0};
    float v[5] = {0};
    signed char e[5] = {0};
    for (int i = 0; i < 3; i++) {
        _magic2_data_st *d = mp->mdp;
        f[1] = (short)d->scl_mfrm[0][i];
        e[1] = d->scl_erp[0][i]; e[2] = d->scl_erp[2][i];
        v[0] = (float)d->scl_key[0][i]; v[1] = (float)d->scl_key[1][i]; v[2] = (float)d->scl_key[3][i];
        mp->scale[i] = (mp->scale0[i] * key_value(t, 2, f, v, e)) / 100.0f;
    }
    for (int i = 0; i < 4; i++) {
        _magic2_data_st *d = mp->mdp;
        f[1] = (short)d->col_mfrm[0][i];
        e[1] = d->col_erp[0][i]; e[2] = d->col_erp[2][i];
        v[0] = (float)d->col_key[0][i]; v[1] = (float)d->col_key[1][i]; v[2] = (float)d->col_key[3][i];
        mp->rgba[i] = ((float)mp->rgba0[i] * key_value(t, 2, f, v, e)) / 100.0f;
    }
}

/* 4 keys per channel */
void DF_MAGIC2_Animkey4(_magic2_st *mp)
{
    PS2_FRAME(0xd0);
    if (!mp->cnt) return;
    int end = key_end_frame(mp->mdp);
    int t = ps2_mod32(mp->cnt, end + 1);
    short f[6] = {0, 0, 0, (short)end, 0, 0};
    float v[6] = {0};
    signed char e[6] = {0};
    for (int i = 0; i < 3; i++) {
        _magic2_data_st *d = mp->mdp;
        f[1] = (short)d->scl_mfrm[0][i]; f[2] = (short)d->scl_mfrm[1][i];
        e[1] = d->scl_erp[0][i]; e[2] = d->scl_erp[1][i]; e[3] = d->scl_erp[2][i];
        for (int k = 0; k < 4; k++) v[k] = (float)d->scl_key[k][i];
        mp->scale[i] = (mp->scale0[i] * key_value(t, 3, f, v, e)) / 100.0f;
    }
    for (int i = 0; i < 4; i++) {
        _magic2_data_st *d = mp->mdp;
        f[1] = (short)d->col_mfrm[0][i]; f[2] = (short)d->col_mfrm[1][i];
        e[1] = d->col_erp[0][i]; e[2] = d->col_erp[1][i]; e[3] = d->col_erp[2][i];
        for (int k = 0; k < 4; k++) v[k] = (float)d->col_key[k][i];
        mp->rgba[i] = ((float)mp->rgba0[i] * key_value(t, 3, f, v, e)) / 100.0f;
    }
}

/* 2 keys (key 0 -> key 3) over the whole effect, eased by erp[2]; scale *= scale0 / 100, rgba *= rgba0 / 100 */
void DF_MAGIC2_Animkey2(_magic2_st *mp)
{
    PS2_FRAME(0xe0);
    if (!mp->cnt) return;
    int end = key_end_frame(mp->mdp);
    float p0 = (float)ps2_mod32(mp->cnt, end + 1) / (float)end;
    for (int j = 0; j < 3; j++) {
        _magic2_data_st *d = mp->mdp;
        float a = (float)d->scl_key[0][j], b = (float)d->scl_key[3][j];
        mp->scale[j] = a + (b - a) * erp_call(d->scl_erp[2][j], p0);
    }
    for (int j = 0; j < 3; j++) mp->scale[j] = mp->scale[j] * (mp->scale0[j] / 100.0f);
    for (int j = 0; j < 4; j++) {
        _magic2_data_st *d = mp->mdp;
        float a = (float)d->col_key[0][j], b = (float)d->col_key[3][j];
        mp->rgba[j] = a + (b - a) * erp_call(d->col_erp[2][j], p0);
    }
    for (int j = 0; j < 4; j++) mp->rgba[j] = mp->rgba[j] * ((float)mp->rgba0[j] / 100.0f);
}

/* ---------------------------------------------------------------- matrices */

/* Direction vector -> (pitch, yaw, 0, 1); straight up/down gives -+pi/2. */
void DF_MAGIC2_Vec2Ang(float *ang, float *vec)
{
    float h = ps2_sqrt(vec[0] * vec[0] + vec[2] * vec[2]);
    if (h == 0.0f) {
        if (vec[1] < 0.0f) ang[0] = ps2_u2f(0x3fc90fdb);
        else if (!(vec[1] <= 0.0f)) ang[0] = ps2_u2f(0xbfc90fdb);
        else ang[0] = 0.0f;
    } else {
        ang[0] = ps2_atan2f(vec[1], h);
    }
    ang[1] = ps2_atan2f(vec[0], vec[2]);
    ang[2] = 0.0f;
    ang[3] = 1.0f;
}

/* pitch/yaw of a matrix's z row into SPR 0x1080/0x1084 (0x1088 = 0) */
static void zrow_angles(const float *z)
{
    float *a = SPR_F(0x1080);
    a[0] = ps2_atan2f(z[1], ps2_sqrt(z[2] * z[2] + z[0] * z[0]));
    a[1] = ps2_atan2f(z[0], z[2]);
    a[2] = 0.0f;
}

/* Rotation (z = a[2], x = sx * a[0], y = a[1]) of the identity into m */
static void rot_zxy(float (*m)[4], const float *a, float sx)
{
    acvu0_RotMatrixZ(m, IdentityMtx, a[2]);
    acvu0_RotMatrixX(m, m, sx * a[0]);
    acvu0_RotMatrixY(m, m, a[1]);
}

/* World matrix of an effect (DF_MAGIC2_Main_Matrix; also inlined twice in DF_MAGIC2_Main).
 * flag 0x80: attached to an object (ocmp, optionally under its parent matrix); oya_xyz 0 = full matrix,
 * 1 = position + direction of the z axis only, 2 = position only. Otherwise the effect's own matrix from its
 * velocity direction and loc, put under mat0 the same way. */
static void calc_matrix(_magic2_st *mp)
{
    float (*s1000)[4] = SPR_M(0x1000), (*s1040)[4] = SPR_M(0x1040);
    float *a = SPR_F(0x1080);
    if (mp->flag & 0x80) {
        ObjCMatSt *oc = mp->ocmp;
        float (*parent)[4][4] = oc ? oc->parentmatp : 0;
        if (parent) {
            char xyz = mp->mdp->oya_xyz;
            if (xyz == 2) {
                acvu0_MulMatrix(s1000, *parent, oc->mat);
                acvu0_TransMatrix(mp->matrix, IdentityMtx, s1000[3]);
            } else if (xyz == 1) {
                acvu0_MulMatrix(s1000, *parent, oc->mat);
                zrow_angles(s1000[2]);
                a[3] = 1.0f;
                rot_zxy(s1040, a, -1.0f);
                acvu0_TransMatrix(mp->matrix, s1040, s1000[3]);
            } else {
                acvu0_MulMatrix(mp->matrix, *parent, oc->mat);
            }
        } else {
            char xyz = mp->mdp->oya_xyz;
            if (xyz == 0) {
                acvu0_CopyMatrix(mp->matrix, oc->mat);
            } else if (xyz == 1) {
                zrow_angles(mp->ocmp->mat[2]);
                rot_zxy(s1000, a, -1.0f);
                acvu0_TransMatrix(mp->matrix, s1000, mp->ocmp->mat[3]);
            } else if (xyz == 2) {
                acvu0_TransMatrix(mp->matrix, IdentityMtx, mp->ocmp->mat[3]);
            }
        }
        mp->mat0 = &mp->matrix;
        return;
    }
    DF_MAGIC2_Vec2Ang(a, mp->n_speed);
    rot_zxy(s1000, a, 1.0f);
    if (!mp->mat0) {
        acvu0_TransMatrix(mp->matrix, s1000, mp->loc);
        return;
    }
    acvu0_TransMatrix(s1000, s1000, mp->loc);
    char xyz = mp->mdp->oya_xyz;
    if (xyz == 0) {
        acvu0_MulMatrix(mp->matrix, *mp->mat0, s1000);
    } else if (xyz == 1) {
        zrow_angles((*mp->mat0)[2]);
        rot_zxy(s1040, a, -1.0f);
        acvu0_TransMatrix(s1040, s1040, (*mp->mat0)[3]);
        acvu0_MulMatrix(mp->matrix, s1040, s1000);
    } else {
        acvu0_TransMatrix(s1040, IdentityMtx, (*mp->mat0)[3]);
        acvu0_MulMatrix(mp->matrix, s1040, s1000);
    }
}

void DF_MAGIC2_Main_Matrix(_magic2_st *mp) { calc_matrix(mp); }

/* ---------------------------------------------------------------- per-frame update */

static void DF_MAGIC2_PutPointLight(_magic2_st *mp, float range, float *col, int frm)
{
    if (range <= 0.0f || frm <= 0) return;
    float *loc;
    float m[4][4] __attribute__((aligned(16)));
    if (mp->mdp->oya) {
        loc = mp->loc;
    } else {
        ZeroMemory512b(m, 1);
        m[0][0] = mp->scale[0];
        m[1][1] = mp->scale[1];
        m[2][2] = mp->scale[2];
        m[3][3] = 1.0f;
        sceVu0RotMatrixZ(m, m, mp->ang[2]);
        sceVu0RotMatrixX(m, m, -mp->ang[0]);
        sceVu0RotMatrixY(m, m, mp->ang[1]);
        sceVu0TransMatrix(m, m, mp->loc);
        if (mp->mat0) vu_mul_matrix(m, (const float (*)[4])*mp->mat0, (const float (*)[4])m);
        loc = m[3];
    }
    DF_PutPointLight(3, 0, loc, range, frm, col[0], col[1], col[2]);
}

/* Wraps SPR 0x3000 = a + d into [-pi, pi] (the original loops on the scratchpad copy). */
static float wrap_angle(float a, float d)
{
    float *w = SPR_F(0x3000);
    const float pi = ps2_u2f(0x40490fdb), npi = ps2_u2f(0xc0490fdb), two_pi = ps2_u2f(0x40c90fdb);
    *w = a + d;
    while (!(*w <= pi)) *w = *w - two_pi;
    while (*w < npi) *w = *w + two_pi;
    return *w;
}

/* One frame of a live effect: lifetime, fade-out (on < 0), counters, then motion (Main / MainBoost), colour
 * latch, texture animation, point light, blur copies, UV scroll. Inlined twice in the original: in
 * DF_MAGIC2_Proc for every slot, and in DF_MAGIC2_Main for an effect's parent (so parents update first). */
static void magic2_step(_magic2_st *mp)
{
    int hold = 0;
    if (mp->onp && *mp->onp < 0) *mp->onp = -2;
    mp->calc_frame = FRAME;
    if (mp->on < 0) {
        DF_MAGIC2_Main_Matrix(mp);
        if (!(mp->flag & 0x20)) {
            mp->on = mp->on == -1 ? 0 : -1;
            return;
        }
        float sg = mp->scale[2] < 0.0f ? 1.0f : -1.0f;
        mp->scale[2] = mp->scale[2] + sg * mp->oya_speed;
        if (sg < 0.0f) {
            if (mp->scale[2] < 0.0f) mp->scale[2] = 0.0f;
        } else if (!(mp->scale[2] <= 0.0f)) {
            mp->scale[2] = 0.0f;
        }
        mp->frame--;
        if (mp->frame <= 0) mp->on = 0;
        hold = 1;
    }
    if (!(mp->flag & 0x100)) {
        mp->cnt++;
        if (mp->on > 0 && !(mp->flag & 0x40)) {
            if (mp->cnt < 0) return;
            if (!mp->mdp->endress) {
                if (mp->frame >= 0) mp->frame--;
                if (mp->frame < 0) {
                    DF_MAGIC2_Main_Matrix(mp);
                    DF_MAGIC2_Kill(mp);
                    return;
                }
            }
            _magic2_data_st *d = mp->mdp;
            if (d->endress && d->loop) {
                if (!(mp->cnt < d->frame)) mp->cnt = 0;
            } else if (d->frame < mp->cnt) {
                mp->cnt = d->frame;
            }
        }
    }
    if (!(mp->flag & 0x10)) NowFrameMag2P[NowFrameMag2Num++] = mp;
    unsigned short flag = mp->flag;
    if (flag & 0x40) {   /* fading: rgba -= pos0 / frame per frame */
        if (!(mp->cnt < mp->frame)) {
            DF_MAGIC2_Kill(mp);
            return;
        }
        for (int i = 0; i < 4; i++) mp->rgba[i] = mp->rgba[i] - ps2_div(mp->pos0[i], (float)mp->frame);
        acvu0_CopyVectorF(mp->rgbai[Magic2RgbaiSW], mp->rgba);
    } else {
        if (!hold) {
            if (flag & 0x100) DF_MAGIC2_MainBoost(mp);
            else DF_MAGIC2_Main(mp);
        }
        acvu0_CopyVectorF(mp->rgbai[Magic2RgbaiSW], mp->rgba);
        if (mp->bokashi_id) DF_SHAKE_SetPos(mp->bokashi_id, mp->matrix[3]);
        if (mp->tex_anim && mp->compTBP && mp->addTBP > 0) {
            short add = mp->addTBP;
            if (mp->tafrm < mp->tawait) {
                mp->tafrm++;
            } else {
                if (mp->drawTBP < mp->lasTBP) {
                    mp->drawTBP = mp->drawTBP + add;
                    if (mp->mdp->clut) mp->drawCBP++;
                } else if (!mp->mdp->clamp) {
                    mp->drawTBP = mp->iniTBP;
                    if (mp->mdp->clut) mp->drawCBP = mp->iniCBP;
                }
                mp->tafrm = 0;
            }
        }
        mp->tex_anim = 1;
        if (mp->ptime && !(mp->mdp->pR <= 0.0f)) {
            float col[4];
            for (int i = 0; i < 4; i++) col[i] = (float)mp->mdp->pcolor[i];
            if (mp->ptime > 0) mp->ptime--;
            DF_MAGIC2_PutPointLight(mp, mp->mdp->pR, col, 1);
        }
        if (mp->mdp->blr_time && (!(mp->flag & 0x100) || mp->cnt) &&
            ps2_mod32(mp->cnt, mp->mdp->blr_int) == 0)
            DF_MAGIC2_MakeBlur(mp, mp->mdp->blr_time);
    }
    short u = mp->mdp->u_scr_frame;
    if (u) {
        float s = mp->add_uv[0] + 1.0f / (float)u;
        mp->add_uv[0] = s;
        if (s < 0.0f) s = -s;
        if (!(s < 1.0f)) mp->add_uv[0] = 0.0f;
    }
    short v = mp->mdp->v_scr_frame;
    if (v) {
        float s = mp->add_uv[1] + 1.0f / (float)v;
        mp->add_uv[1] = s;
        if (s < 0.0f) s = -s;
        if (!(s < 1.0f)) mp->add_uv[1] = 0.0f;
    }
}

/* Per-frame update of all effects: spawns pending duplicates, then steps every live slot. */
void DF_MAGIC2_Proc(void)
{
    PS2_FRAME(0x70);
    NowFrameMag2Num = 0;
    Magic2RgbaiSW = Magic2RgbaiSW == 0;
    for (_duplicate_st_ *dp = duplicate_top; dp; dp = dp->next) {
        DF_MAGIC2_MakeMagic(dp);
        if (dp->n) continue;
        if (dp->before) dp->before->next = dp->next;
        else duplicate_top = dp->next;
        if (dp->next) dp->next->before = dp->before;
        else duplicate_now = dp->before;
        dp->n = 0;
        unsigned short id = dp->id;
        if (id && id <= 0x200) {
            dp->id = 0;
            if (StillLiveID[id - 1]) StillLiveID[id - 1]--;
        }
    }
    for (int i = 1; i < 0x200; i++) {
        _magic2_st *mp = &Mag2St[i];
        if (mp->on && (unsigned)mp->calc_frame < FRAME) magic2_step(mp);
    }
}

/* Motion of a normal (non-boost) effect: integrate velocity, spin angles, camera-facing roll, gravity / drag /
 * acceleration, update the parent first, rebuild the matrix when needed, run the key animation, stretch mode
 * (flag 0x20) and spawn child effects at their frames. */
int DF_MAGIC2_Main(_magic2_st *mp)
{
    PS2_FRAME(0xd0);
    int have_matrix = 0;
    _magic2_data_st *d0 = mp->mdp;
    for (int i = 0; i < 3; i++) mp->loc[i] = mp->loc[i] + mp->rotspd[i];
    if (d0->ang_opt == 1) {   /* face the direction of travel */
        *SPR_F(0x3000) = ps2_sqrt(mp->n_speed[0] * mp->n_speed[0] + mp->n_speed[2] * mp->n_speed[2]);
        mp->ang[0] = ps2_atan2f(mp->n_speed[1], *SPR_F(0x3000));
        mp->ang[1] = ps2_atan2f(mp->n_speed[0], mp->n_speed[2]);
    } else {
        float sg = (mp->flag & 0x200) ? -1.0f : 1.0f;
        mp->ang[0] = wrap_angle(mp->ang[0], mp->ang_spd_rnd[0] * sg);
        sg = (mp->flag & 0x200) ? -1.0f : 1.0f;
        mp->ang[1] = wrap_angle(mp->ang[1], mp->ang_spd_rnd[1] * sg);
    }
    float sg = (mp->flag & 0x200) ? -1.0f : 1.0f;
    mp->ang[2] = wrap_angle(mp->ang[2], mp->ang_spd_rnd[2] * sg);

    if (mp->mdp->rot_camera_z != 0.0f) {   /* roll towards player 1's camera side */
        const float *k = magic2_roll_axis;
        float kx = k[0], kz = k[2];
        float dv[4], bv[4];
        dv[0] = mp->loc[0] - g_ac[0].loc[0];
        dv[1] = mp->loc[1] - g_ac[0].loc[1];
        dv[2] = mp->loc[2] - g_ac[0].loc[2];
        float q = vu_rcp_len3(dv);
        dv[0] = dv[0] * q; dv[1] = dv[1] * q; dv[2] = dv[2] * q; dv[3] = 0.0f;
        float x = ps2_u2f(0xbfc90fdb) + g_ac[0].ang[1];
        float c = kx * fsmath_CosVU0(x);
        bv[0] = -(c - kz * fsmath_SinVU0(x));
        bv[1] = 0.0f;
        float s = kx * fsmath_SinVU0(x);
        bv[2] = s + kz * fsmath_CosVU0(x);
        q = vu_rcp_len3(bv);
        bv[0] = bv[0] * q; bv[1] = bv[1] * q; bv[2] = bv[2] * q; bv[3] = 0.0f;
        float ip = InnerProduct(dv, bv);
        mp->ang[2] = mp->mdp->rot_spd[2] + mp->mdp->rot_camera_z * ip;
    }

    mp->rotspd[1] = mp->rotspd[1] + d0->gravity;
    float sp = ps2_sqrt((mp->rotspd[0] * mp->rotspd[0] + mp->rotspd[1] * mp->rotspd[1]) + mp->rotspd[2] * mp->rotspd[2]);
    mp->speed = sp;
    if (!(sp <= 0.0f)) {
        float inv = 1.0f / sp;
        mp->n_speed[0] = mp->rotspd[0] * inv;
        mp->n_speed[1] = mp->rotspd[1] * inv;
        mp->n_speed[2] = mp->rotspd[2] * inv;
        mp->speed = mp->speed - mp->speed * d0->registor;
        mp->speed = mp->speed + d0->accele;
        if (mp->speed < 0.0f) mp->speed = 0.0f;
        float s = mp->speed;
        mp->rotspd[0] = mp->n_speed[0] * s;
        mp->rotspd[1] = mp->n_speed[1] * s;
        mp->rotspd[2] = mp->n_speed[2] * s;
        mp->rotspd[3] = mp->n_speed[3];
    }

    _magic2_st *oya = mp->oya;
    if (oya) {
        if (oya->on < 0) oya->on = -2;
        oya = mp->oya;
        if ((unsigned)oya->calc_frame < FRAME) magic2_step(oya);
    }

    if ((mp->flag & 0x4000) || (mp->flag & 0x80)) {
        calc_matrix(mp);
        have_matrix = 1;
    }
    if (mp->anim_func) anim_call(mp);

    if ((mp->flag & 0x20) && mp->mat0) {   /* stretch towards the start position */
        float *t = SPR_F(0);
        float *p3 = (*mp->mat0)[3];
        t[0] = mp->pos0[0] - p3[0];
        t[1] = mp->pos0[1] - p3[1];
        t[2] = mp->pos0[2] - p3[2];
        t[3] = mp->pos0[3];
        *SPR_F(0x10) = ps2_sqrt(t[2] * t[2] + (t[0] * t[0] + t[1] * t[1]));
        float len = *SPR_F(0x10);
        if (len < d0->param[1]) mp->scale[2] = ps2_div(len, d0->param[0]);
        else mp->scale[2] = ps2_div(d0->param[1], d0->param[0]);
    }

    if (mp->flag & 0x1000) {
        mp->flag &= 0xefff;
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        _magic2_data_st *d = mp->mdp;
        if (!d->child_frm[i] || d->child_mid[i] <= 0 || d->child_frm[i] - 1 != mp->cnt) continue;
        if (!have_matrix) {
            calc_matrix(mp);
            have_matrix = 1;
        }
        int r;
        if (mp->comb) {
            unsigned int ofs = Magic2_Reduce == 1 ? mp->comb->magic_ofs2 : mp->comb->magic_ofs;
            r = DF_MAGIC2_AppearMagic_InMagic2Ptr((_magic2_data_st *)(uintptr_t)(ofs + d0->child_mid[i] * 0x140),
                                                  mp->comb, 0, mp, 4);
        } else {
            unsigned short mid = (unsigned short)mp->mdp->child_mid[i];
            r = mid && mid < 0x200 ? DF_MAGIC2_AppearMagic_InMagic2Ptr(MagicData2 + mid, 0, 0, mp, 4) : 0;
        }
        if (r == -1) return 0;
    }
    return 0;
}

/* ---------------------------------------------------------------- drawing */

static void mkdl_magic(unsigned int **gpp, _magic2_st *magp, int vid)
{
    if (magp && !(magp->flag & 0x2000))
        *gpp = DF_MAGIC2_Draw(*gpp, (char *)magp->tfm, magp, NowFrameMagic2Matrix[magp->mtxid], vid);
}

void DF_MAGIC2_SetFog(unsigned int **gpp, float *foga, float *fogb)
{
    unsigned int *gp = *gpp;
    memcpy(DF_MAGIC2_SetFog__foga0[Magic2MatrixSW], foga, 16);
    memcpy(DF_MAGIC2_SetFog__fogb0[Magic2MatrixSW], fogb, 16);
    GL_Sync(&gp);
    GL_LoadVector(&gp, 0xc, DF_MAGIC2_SetFog__foga0[Magic2MatrixSW]);
    GL_LoadVector(&gp, 0xd, DF_MAGIC2_SetFog__fogb0[Magic2MatrixSW]);
    *gpp = gp;
}

static unsigned int *put_unpack_ref(unsigned int *p, unsigned int unpack, const void *ref)
{
    p[0] = 0x10000001; p[1] = 0;            /* DMA cnt 1 */
    p[2] = 0; p[3] = 0; p[4] = 0; p[5] = 0; p[6] = 0;
    p[7] = unpack;                          /* UNPACK V4-32 x4 */
    p[8] = 0x30000004; p[9] = PS2PTR(ref);  /* DMA ref 4 qw */
    p[10] = 0; p[11] = 0;
    return p + 12;
}

/* Packet of one effect model (tfm): GS test/alpha/clamp registers, the effect matrix and colour, then per batch
 * its matrix (UNPACK to VU 4) and a DMA call of its geometry; the first textured batch swaps TBP/CBP for texture
 * animation and loads TEX0 into both contexts, later batches clear it again. */
unsigned int *DF_MAGIC2_Draw(unsigned int *gp, char *tfm, _magic2_st *magp, float (*mat)[4], int viewID)
{
    /* The original passes a zeroed stack buffer here, which GL_LoadVector only references (DMA ref, read when
     * the list is kicked later); a static zero vector gives the intended "no texture" deterministically. */
    static const unsigned long long zero_tex[2] __attribute__((aligned(16))) = {0, 0};
    int tex_loaded = 0, tex_done = 0;
    unsigned int *p = gp;
    if (!tfm || ((magp->f_nodraw >> viewID) & 1)) return gp;
    unsigned int *batch = (unsigned int *)(tfm + 0x10);
    unsigned int *bmat = batch + *(int *)(tfm + 4) * 4;
    memcpy(magp->load_rgbai, magp->rgbai[Magic2RgbaiSW], 16);
    magp->load_rgbai[3] = magp->rgbai[Magic2RgbaiSW][3] * magp->draw_alpha_rate;

    p[0] = 0x10000005; p[1] = 0; p[2] = 0; p[3] = 0; p[4] = 0; p[5] = 0; p[6] = 0;
    p[7] = 0x51000004;                             /* DIRECT 4 */
    p[8] = 0x8003; p[9] = 0x10000000; p[10] = 0xe; p[11] = 0;   /* GIF A+D x3, EOP */
    memcpy(p + 12, &magp->mdp->gsTest, 8);  p[14] = 0x47; p[15] = 0;   /* TEST_1 */
    memcpy(p + 16, &magp->mdp->gsAlpha, 8); p[18] = 0x42; p[19] = 0;   /* ALPHA_1 */
    p[20] = 0; p[21] = 0; p[22] = 8; p[23] = 0;                        /* CLAMP_1 = 0 */
    p += 24;
    GL_Sync(&p);
    p = put_unpack_ref(p, 0x6c040000, mat);
    GL_LoadVector(&p, 0xb, magp->load_rgbai);
    GL_LoadVector(&p, 0xe, magp->add_uv);
    for (unsigned int i = 0; i < *(unsigned int *)(tfm + 4); i++, batch += 4, bmat += 16) {
        p = put_unpack_ref(p, 0x6c040004, bmat);
        unsigned long long *texp = (unsigned long long *)(uintptr_t)batch[2];
        if (!tex_done && (*(unsigned int *)(tfm + 8) & 1) && texp) {
            unsigned long long t0 = *texp;
            unsigned long long *bufp = magp->tex[Magic2MatrixSW];
            CopyMemory32b(bufp, &t0, 2);
            unsigned long long v = bufp[0];
            if (magp->compTBP == (long long)(v & 0x3fff)) {
                bufp[0] = (v & ~0x3fffULL) | (unsigned long long)(long long)magp->drawTBP;
                bufp[0] = (bufp[0] & 0xfff8001fffffffffULL) | ((unsigned long long)(long long)magp->drawCBP << 37);
            }
            bufp[1] = 6;   /* A+D address: TEX0_1 */
            tex_done = 1;
            GL_LoadVector(&p, 0x180, &DF_MAGIC2_Draw__tex0_gif);
            GL_LoadVector(&p, 0x181, bufp);
            GL_LoadVector(&p, 0x340, &DF_MAGIC2_Draw__tex0_gif);
            GL_LoadVector(&p, 0x341, bufp);
            tex_loaded = 1;
        } else if (tex_loaded) {
            GL_LoadVector(&p, 0x181, (void *)zero_tex);
            GL_LoadVector(&p, 0x341, (void *)zero_tex);
            tex_loaded = 0;
        }
        p[0] = 0x50000000; p[1] = batch[0];   /* DMA call */
        p[2] = 0; p[3] = 0;
        p += 4;
        GL_Sync(&p);
    }
    return p;
}

/* ---------------------------------------------------------------- per-frame matrices / visibility */

/* LW = diag(scale * scale_rate) rotated: mode 1 = full Z/X/Y rotation, else z roll only (billboard) */
static void local_scale(_magic2_st *data, int full)
{
    float (*lw)[4] = *LWMAT_SPR;
    lw[0][0] = data->scale[0] * data->scale_rate;
    lw[1][1] = data->scale[1] * data->scale_rate;
    if (full) lw[2][2] = data->scale[2] * data->scale_rate;
}

/* Builds every visible effect's world->screen matrix into NowFrameMagic2Matrix[i] (LWMAT_SPR keeps its local->world
 * matrix), frustum-tests its bounding box (tfm+0xc: centre, extents) when Magic2_Clip is on (flag 0x2000 = culled),
 * fades view-angle dependent effects and registers it for depth-sorted translucent drawing via mkdl_magic. */
void DF_Magic2MatrixUpdate(float (*wvmat)[4], int isCockpitView)
{
    sceVu0CopyMatrix(*WSMAT_SPR, WORLD_SCREEN_MATRIX);
    sceVu0CopyMatrix(*CMAT2_SPR, CAMERA_MATRIX2);
    NowFrameMagic2Matrix = Magic2Matrix[Magic2MatrixSW][Magic2MatrixSC];
    for (int i = 0; i < NowFrameMag2Num; i++) {
        _magic2_st *data = NowFrameMag2P[i];
        if (!data->tfm) continue;
        float (*out)[4] = NowFrameMagic2Matrix[i];
        float (*lw)[4] = *LWMAT_SPR;
        if (data->mat0 && (data->flag & 1)) {   /* matrix supplied as is */
            vu_mul_matrix(out, (const float (*)[4])*WSMAT_SPR, (const float (*)[4])*data->mat0);
            sceVu0CopyMatrix(lw, *data->mat0);
        } else if (data->mat0 && (data->flag & 4)) {   /* relative to mat0 */
            float *tmp = (float *)(LWMAT_SPR + 1);
            acvu0_CopyMatrix(lw, IdentityMtx);
            if (data->mdp->mode == 1) {
                local_scale(data, 1);
                acvu0_RotMatrixZ(lw, lw, data->ang[2]);
                acvu0_RotMatrixX(lw, lw, -data->ang[0]);
                acvu0_RotMatrixY(lw, lw, data->ang[1]);
                if (data->mdp->ang_opt == 2) {
                    acvu0_ApplyMatrix(tmp, *data->mat0, data->loc);
                    acvu0_TransMatrix(lw, lw, tmp);
                } else {
                    acvu0_TransMatrix(lw, lw, data->loc);
                    acvu0_MulMatrix(lw, *data->mat0, lw);
                }
            } else {
                local_scale(data, 0);
                acvu0_RotMatrixZ(lw, lw, data->ang[2]);
                acvu0_MulMatrix(lw, *CMAT2_SPR, lw);
                acvu0_ApplyMatrix(tmp, *data->mat0, data->loc);
                acvu0_TransMatrix(lw, lw, tmp);
            }
            acvu0_MulMatrix(out, *WSMAT_SPR, lw);
            out[3][2] = out[3][2] + data->mdp->addz;
        } else {   /* world position (mat0 not used) */
            sceVu0CopyMatrix(lw, IdentityMtx);
            if (data->mdp->mode == 1) {
                local_scale(data, 1);
                sceVu0RotMatrixZ(lw, lw, data->ang[2]);
                sceVu0RotMatrixX(lw, lw, -data->ang[0]);
                sceVu0RotMatrixY(lw, lw, data->ang[1]);
                sceVu0TransMatrix(lw, lw, data->loc);
            } else {
                local_scale(data, 0);
                sceVu0RotMatrixZ(lw, lw, data->ang[2]);
                vu_mul_matrix(lw, (const float (*)[4])*CMAT2_SPR, (const float (*)[4])lw);
                sceVu0TransMatrix(lw, lw, data->loc);
            }
            vu_mul_matrix(out, (const float (*)[4])*WSMAT_SPR, (const float (*)[4])lw);
            out[3][2] = out[3][2] + data->mdp->addz;
        }

        if (!Magic2_Clip) {
            NowFrameMag2P[i]->flag &= 0xdfff;
        } else {
            const float kx = 0.5f, ky = ps2_u2f(0x3eb95810);   /* half-width / half-height of the view frustum */
            const float cx = ps2_sqrt(1.0f + kx * kx), cy = ps2_sqrt(1.0f + ky * ky);
            NowFrameMag2P[i]->flag &= 0xdfff;
            int visible = 1;
            const float *bb = *(float **)((char *)data->tfm + 0xc);
            if (CAMERA_MATRIX1[3][3] != 0.0f && bb) {
                float p[4];
                acvu0_ApplyMatrix(p, lw, (float *)bb);
                p[3] = 1.0f;
                vu_apply(p, (const float (*)[4])CAMERA_MATRIX1, p);
                float r = 0.0f;
                for (int k = 0; k < 3; k++) {
                    float e = data->scale[k] * bb[4 + k];
                    if (e < 0.0f) e = -e;
                    if (r < e) r = e;
                }
                float z = p[2], x = p[0], y = p[1];
                visible = 0;
                if (!(z <= -r)) {
                    float hx = kx * z + r * cx, hy = ky * z + r * cy;
                    if (x * x <= hx * hx && y * y <= hy * hy) {
                        int part = z < r;
                        if (z < r * ps2_u2f(0x3f880000)) {
                            float nx = kx * z - r * cx, ny = ky * z - r * cy;
                            if (!(x <= nx)) part = 1;
                            if (x < -nx) part = 1;
                            if (!(y <= ny)) part = 1;
                            if (y < -ny) part = 1;
                        }
                        visible = !part;
                    }
                }
            }
            if (!visible) NowFrameMag2P[i]->flag |= 0x2000;
        }

        if (data->mdp->view_alpha && data->mat0) {   /* fade by the angle between view and effect axis */
            float vv[4], ax[4];
            float q = vu_rcp_len3(CUR_DRAW_CAM->vvec);
            vv[0] = CUR_DRAW_CAM->vvec[0] * q; vv[1] = CUR_DRAW_CAM->vvec[1] * q;
            vv[2] = CUR_DRAW_CAM->vvec[2] * q; vv[3] = 0.0f;
            DF_GetSRTXZY(*data->mat0, 0, ax, 0);
            float (*m)[4] = SPR_M(0x3000);
            sceVu0CopyMatrix(m, IdentityMtx);
            sceVu0RotMatrixX(m, m, ax[0]);
            sceVu0RotMatrixY(m, m, ax[1]);
            sceVu0RotMatrixX(m, m, data->ang[0]);
            sceVu0RotMatrixY(m, m, data->ang[1]);
            q = vu_rcp_len3(m[2]);
            ax[0] = m[2][0] * q; ax[1] = m[2][1] * q; ax[2] = m[2][2] * q; ax[3] = 0.0f;
            float ip = sceVu0InnerProduct(vv, ax);
            data->alpha_rate = 0.0f;
            if (!(ip <= 0.0f)) {
                float a = ip * ip;
                data->alpha_rate = a * (a * (a * a));
            }
        }
        data->mtxid = (short)i;

        float c[4];
        const float *bc = *(float **)((char *)data->tfm + 0xc);
        if (!bc) bc = (const float *)PS2_NULL_MEM();   /* the original reads address 0 */
        vu_apply(c, (const float (*)[4])lw, bc);
        c[3] = 1.0f;
        vu_apply(c, (const float (*)[4])wvmat, c);
        float z = c[2];
        if (!data->mdp->no_near_cut) {
            if (z <= g_distMagCutCmplt) continue;
            if (z <= g_distMagCutStart) data->draw_alpha_rate = s_rateMagCutAlpha * (data->alpha_rate * (z - g_distMagCutCmplt));
            else data->draw_alpha_rate = data->alpha_rate;
        } else if (isCockpitView && data->mdp->cockpit_against) {
            data->draw_alpha_rate = ps2_u2f(0x3e666666) * data->alpha_rate;
        } else {
            data->draw_alpha_rate = data->alpha_rate;
        }
        DF_MkDL_RegistXLU(z - data->mdp->addz, (void *)GFN(mkdl_magic), data);
    }
}

/* ---------------------------------------------------------------- helpers */

static const float k_pi = ps2_u2f(0x40490fdb), k_npi = ps2_u2f(0xc0490fdb), k_2pi = ps2_u2f(0x40c90fdb);

/* the angle wrap loops of the original ([-pi, pi]) */
static float wrap_pi(float a)
{
    while (!(a <= k_pi)) a = a - k_2pi;
    while (a < k_npi) a = a + k_2pi;
    return a;
}

static void copy4(float *d, const float *s) { memcpy(d, s, 16); }

/* (0x7fff - (rand & 0xffff)) / 32767: a random factor in [-1, 1] */
static float rnd_sym(void)
{
    unsigned int r = (unsigned int)ps2_rand() & 0xffff;
    return (float)(int)(0x7fff - r) / 32767.0f;
}

/* rand() % 0x8000 < lim (the option 0x400 / 0x8000 spawn chance) */
static int rand_pass(int lim)
{
    int r = ps2_rand(), m = r & 0x7fff;
    if (r < 0 && m) m -= 0x8000;
    return m < lim;
}

/* float -> unsigned as MWCC converts it (values >= 2^31 through the signed range) */
static unsigned int f2u(float f)
{
    if (2147483648.0f <= f) return (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u;
    return (unsigned int)ps2_ftoi(f);
}

static unsigned int comb_magic_ofs(const _stSfxCombHead *comb)
{
    return Magic2_Reduce == 1 ? comb->magic_ofs2 : comb->magic_ofs;
}

static _magic2_data_st *comb_magic(const _stSfxCombHead *comb, int mid)
{
    return (_magic2_data_st *)(uintptr_t)(comb_magic_ofs(comb) + mid * 0x140);
}

/* mdp outside the combined file's magic table */
static int comb_bad(const _magic2_data_st *mdp, const _stSfxCombHead *comb)
{
    unsigned int idx = (PS2PTR(mdp) - comb_magic_ofs(comb)) / 0x140;
    return !((int)idx < comb->magic_num);
}

/* a new effect id: the first unused StillLiveID entry */
static void id_new(unsigned short *id)
{
    *id = 0;
    for (int i = 0; i < 0x200; i++) {
        if (!StillLiveID[i]) {
            StillLiveID[i] = StillLiveID[i] + 1;
            *id = (unsigned short)(i + 1);
            return;
        }
    }
}

/* share a parent's id (one more user) if it is still live */
static void id_share(unsigned short *dst, unsigned short id)
{
    if (!id || id >= 0x201) return;
    *dst = 0;
    unsigned short n = StillLiveID[id - 1];
    if (n) {
        StillLiveID[id - 1] = n + 1;
        *dst = id;
    }
}

static void df_magic2_ID_delete(unsigned short *id)
{
    if (!id) return;
    unsigned short v = *id;
    if (!v || v >= 0x201) return;
    *id = 0;
    unsigned short n = StillLiveID[v - 1];
    if (n) StillLiveID[v - 1] = n - 1;
}

static int id_live(int id) { return id && id < 0x201 && StillLiveID[id - 1]; }

/* first free duplicate (n == 0), zeroed and appended to the list */
static _duplicate_st_ *dup_alloc(void)
{
    _duplicate_st_ *dp = s_Duplicate;
    int i;
    for (i = 0; i < 0x80; i++, dp++)
        if (!dp->n) break;
    if (i >= 0x80) return 0;
    ZeroMemory32b(dp, 0x20);
    if (duplicate_now) {
        duplicate_now->next = dp;
        dp->before = duplicate_now;
        duplicate_now = dp;
    } else {
        duplicate_top = dp;
        duplicate_now = dp;
    }
    return dp;
}

static void dup_unlink(_duplicate_st_ *dp)
{
    if (dp->before) dp->before->next = dp->next;
    else duplicate_top = dp->next;
    if (dp->next) dp->next->before = dp->before;
    else duplicate_now = dp->before;
}

static void dup_set_n(_duplicate_st_ *dp, const _magic2_data_st *mdp)
{
    dp->n = (unsigned char)mdp->nloop;
    if (!dp->n) dp->n = 1;
}

/* the optional pos / ang / scl / col overrides of a duplicate (flag 1 / 2 / 4 / 8) */
static void dup_set_vectors(_duplicate_st_ *dp, float *pos, float *ang, float *scl, float *col)
{
    if (pos) { dp->flag |= 1; copy4(dp->pos, pos); }
    if (ang) { dp->flag |= 2; copy4(dp->ang, ang); }
    if (scl) { dp->flag |= 4; copy4(dp->scl, scl); }
    if (col) { dp->flag |= 8; copy4(dp->col, col); }
}

/* ---------------------------------------------------------------- misc */

char *Magic_GetModel(int sfxid) { return (char *)Magic2_tfm[sfxid]; }

void MAGIC2_DeleteAllMagic(void)
{
    PS2_FRAME(0x30);
    for (int i = 1; i < 0x200; i++) {
        _magic2_st *mp = &Mag2St[i];
        if (!(mp->flag & 0x100)) {
            DF_MAGIC2_Kill(mp);
            continue;
        }
        mp->boost_sw = 0;
        mp->boostaddsub = 0;
        mp->boostaddsub_old = 0;
        mp->cnt = 0;
        mp->flag |= 0x10;
        if (mp->bokashi_id) DF_SHAKE_SetSW(mp->bokashi_id, 0);
    }
    ZeroMemory32b(s_Duplicate, 0x1000);
    for (int i = 0; i < 0x80; i++) s_Duplicate[i].id = 0;
    duplicate_top = 0;
    duplicate_now = 0;
}

int DF_MAGIC2_GetMagicReduce(void) { return Magic2_Reduce == 1; }

_magic2_st *DF_MAGIC2_SearchID(int num)
{
    if (!num || !(num < 0x201)) return 0;
    _magic2_st *mp = Mag2St + 1;
    for (int i = 1; i < 0x200; i++, mp++)
        if (mp->id == num) return mp;
    return 0;
}

/* view mask bit viewID of a boost effect and its sibling chain */
void DF_MAGIC2_BoostSetHide(_magic2_st *mp, int viewID, int hide)
{
    char clr = (char)~(1 << viewID), set = (char)((hide != 0) << viewID);
    for (; mp; mp = mp->sibling) {
        mp->f_nodraw = (char)(mp->f_nodraw & clr);
        mp->f_nodraw = (char)(mp->f_nodraw | set);
    }
}

void DF_MAGIC2_Boost(_magic2_st *mp, int sw)
{
    for (; mp; mp = mp->sibling) mp->boost_sw = sw > 0;
}

/* ---------------------------------------------------------------- boost effects */

static void boost_show(_magic2_st *mp)
{
    mp->flag &= 0xffef;
    if (mp->bokashi_id) DF_SHAKE_SetSW(mp->bokashi_id, 1);
}

/* cnt runs up to scl_mfrm[1][0] and back down to scl_mfrm[0][0] while the boost is held */
static void boost_pingpong(_magic2_st *mp, int m0, int m1)
{
    if (mp->boostaddsub == 0) {
        mp->cnt++;
        if (!(mp->cnt < m1)) mp->boostaddsub = 1;
    } else {
        mp->cnt--;
        if (!(m0 < mp->cnt)) mp->boostaddsub = 0;
    }
    mp->boostaddsub_old = mp->boostaddsub;
}

/* child_mid[0] effect every param[0] frames while boosting (param[0] is the MagicData2 id without a comb) */
static void boost_spawn(_magic2_st *mp, _magic2_data_st *d, float p)
{
    _stSfxCombHead *comb = mp->comb;
    if (comb) {
        _magic2_data_st *md = comb_magic(comb, d->child_mid[0]);
        if (mp->flag & 0x80) DF_MAGIC2_AppearMagicOCMPPtr(md, comb, mp->ocmp, 4);
        else if (mp->mat0) DF_MAGIC2_AppearMagicPtr(md, comb, 0, 0, 0, 0, mp->mat0, 0, 4);
        else DF_MAGIC2_AppearMagic_InMagic2Ptr(md, comb, 0, mp, 4);
        return;
    }
    if (mp->flag & 0x80) {
        unsigned int mid = f2u(p) & 0xffff;
        if (mid && mid < 0x200 && mp->ocmp) DF_MAGIC2_AppearMagicOCMPPtr(MagicData2 + mid, 0, mp->ocmp, 4);
    } else if (mp->mat0) {
        float (*mat0)[4][4] = mp->mat0;
        unsigned int mid = f2u(p) & 0xffff;
        if (mid && mid < 0x200) DF_MAGIC2_AppearMagicPtr(MagicData2 + mid, 0, 0, 0, 0, 0, mat0, 0, 4);
    } else {
        unsigned int mid = f2u(p) & 0xffff;
        if (mid && mid < 0x200) DF_MAGIC2_AppearMagic_InMagic2Ptr(MagicData2 + mid, 0, 0, mp, 4);
    }
}

/* Motion of a boost effect (flag 0x100): key frame ping-pong while boost_sw is held (boostaddsub 0 up, 1 down,
 * 2 = released, running out to the end), hidden (flag 0x10) when stopped, then the same motion as DF_MAGIC2_Main
 * without drag clamping. */
int DF_MAGIC2_MainBoost(_magic2_st *mp)
{
    PS2_FRAME(0x60);
    _magic2_data_st *d = mp->mdp;
    if (mp->bokashi_id) DF_SHAKE_SetSW(mp->bokashi_id, mp->cnt > 0 ? 1 : 0);
    if (mp->boost_sw && d->child_mid[0] > 0) {
        float p = d->param[0];
        if (p != 0.0f) {
            unsigned int dv = (unsigned int)ps2_ftoi(p);
            if ((dv ? FRAME % dv : FRAME) == 0) boost_spawn(mp, d, p);
        }
    }

    int m0 = d->scl_mfrm[0][0], m1 = d->scl_mfrm[1][0];
    if (!mp->boost_sw) {   /* released: run on to the end */
        if (mp->cnt == 0) {
            mp->flag |= 0x10;
            if (mp->bokashi_id) DF_SHAKE_SetSW(mp->bokashi_id, 0);
            return 0;
        }
        if (mp->cnt > 0) {
            boost_show(mp);
            mp->cnt++;
            mp->boostaddsub = 2;
        }
    } else if (mp->boostaddsub == 2) {   /* pressed again while running out */
        int c = mp->cnt;
        if (c < m0) {
            boost_show(mp);
            mp->cnt++;
            mp->boostaddsub = 0;
        } else if (m1 < c) {
            boost_show(mp);
            mp->cnt--;
            if (mp->cnt == m1) mp->boostaddsub = 1;
        } else {
            mp->boostaddsub = mp->boostaddsub_old;
            if (mp->boostaddsub == 0) {
                if (!(mp->cnt < m1)) mp->boostaddsub = 1;
            } else if (!(m0 < mp->cnt)) {
                mp->boostaddsub = 0;
            }
            boost_show(mp);
            boost_pingpong(mp, m0, m1);
        }
    } else {
        int c = mp->cnt;
        if (c < m0) {
            boost_show(mp);
            mp->cnt++;
            mp->boostaddsub = 0;
        } else if (!(m1 < c)) {
            boost_show(mp);
            boost_pingpong(mp, m0, m1);
        } else {
            boost_show(mp);
            mp->boostaddsub = 0;
            mp->cnt = 0;
        }
    }
    if (d->frame < mp->cnt) {
        mp->flag |= 0x10;
        if (mp->bokashi_id) DF_SHAKE_SetSW(mp->bokashi_id, 0);
        mp->boostaddsub = 0;
        mp->cnt = 0;
        return 0;
    }
    if (mp->anim_func) anim_call(mp);

    for (int i = 0; i < 3; i++) mp->loc[i] = mp->loc[i] + mp->rotspd[i];
    float sg = (mp->flag & 0x200) ? -1.0f : 1.0f;
    if (d->ang_opt == 1) {
        *SPR_F(0x3000) = ps2_sqrt(mp->n_speed[0] * mp->n_speed[0] + mp->n_speed[2] * mp->n_speed[2]);
        mp->ang[0] = ps2_atan2f(mp->n_speed[1], *SPR_F(0x3000));
        mp->ang[1] = ps2_atan2f(mp->n_speed[0], mp->n_speed[2]);
        mp->ang[2] = wrap_angle(mp->ang[2], mp->ang_spd_rnd[2] * sg);   /* and again below */
    } else {
        mp->ang[0] = wrap_angle(mp->ang[0], mp->ang_spd_rnd[0] * sg);
        mp->ang[1] = wrap_angle(mp->ang[1], mp->ang_spd_rnd[1] * sg);
    }
    mp->ang[2] = wrap_angle(mp->ang[2], mp->ang_spd_rnd[2] * sg);

    mp->rotspd[1] = mp->rotspd[1] + d->gravity;
    float sp = ps2_sqrt((mp->rotspd[0] * mp->rotspd[0] + mp->rotspd[1] * mp->rotspd[1]) + mp->rotspd[2] * mp->rotspd[2]);
    mp->speed = sp;
    if (!(sp <= 0.0f)) {
        float inv = 1.0f / sp;
        mp->n_speed[0] = mp->rotspd[0] * inv;
        mp->n_speed[1] = mp->rotspd[1] * inv;
        mp->n_speed[2] = mp->rotspd[2] * inv;
        mp->speed = mp->speed - mp->speed * d->registor;
        mp->speed = mp->speed + d->accele;
    }

    _magic2_st *oya = mp->oya;
    if (oya) {
        if (oya->on < 0) oya->on = -2;
        oya = mp->oya;
        if ((unsigned)oya->calc_frame < FRAME) magic2_step(oya);
    }
    calc_matrix(mp);

    if (mp->flag & 0x1000) {
        mp->flag &= 0xefff;
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        _magic2_data_st *dd = mp->mdp;
        unsigned short frm = dd->child_frm[i];
        short mid = dd->child_mid[i];
        if (!frm || mid <= 0 || frm - 1 != mp->cnt) continue;
        int r;
        if (mp->comb) {
            r = DF_MAGIC2_AppearMagic_InMagic2Ptr(comb_magic(mp->comb, mid), mp->comb, 0, mp, 4);
        } else {
            unsigned short m = (unsigned short)mid;
            r = m && m < 0x200 ? DF_MAGIC2_AppearMagic_InMagic2Ptr(MagicData2 + m, 0, 0, mp, 4) : 0;
        }
        if (r == -1) return 0;
    }
    return 0;
}

/* ---------------------------------------------------------------- spawning */

/* fading copy of mp0 at its current matrix, alive for time frames (flag 0x40) */
static _magic2_st *DF_MAGIC2_MakeBlur(_magic2_st *mp0, int time)
{
    PS2_FRAME(0x50);
    int num;
    _magic2_st *mp = DF_MAGIC2_GetFreeMagic(&num);
    if (!mp) return 0;
    DF_MAGIC2_Init_MP(mp);
    mp->on = (short)num;
    mp->tfm = mp0->tfm;
    mp->mdp = mp0->mdp;
    mp->frame = (short)time;
    mp->flag = mp0->flag | 0x40;
    mp->flag = mp->flag & 0x240;
    copy4(mp->loc, mp0->matrix[3]);
    MatrixZtoAng(mp->ang, mp0->matrix);
    mp->ang[3] = 1.0f;
    copy4(mp->scale, mp0->scale);
    copy4(mp->scale0, mp0->scale0);
    copy4(mp->rgba, mp0->rgba);
    CopyMemory32b(mp->rgba0, mp0->rgba0, 1);
    copy4(mp->pos0, mp0->rgba);
    id_new(&mp->id);
    return mp;
}

/* One duplicate per frame: every interval frames emit `with` effects from dp (n counts down the emissions). */
static void DF_MAGIC2_MakeMagic(_duplicate_st_ *dp)
{
    PS2_FRAME(0x80);
    _magic2_data_st *mdp = dp->mdp;
    if (dp->onp && *dp->onp < 0) *dp->onp = -2;
    if (mdp == MagicData2) {
        dp->n = 0;
        return;
    }
    if (!((unsigned)dp->makeframe < FRAME)) return;
    dp->makeframe = FRAME;
    if (dp->oya && dp->oya->on < 0) dp->oya->on = -2;
    int iv = mdp->interval ? mdp->interval : 1;
    dp->frame++;
    if (ps2_mod32(dp->frame - 1, iv) != 0) return;
    for (int i = 0; i < (mdp->with ? mdp->with : 1); i++) {
        int num;
        _magic2_st *mp = DF_MAGIC2_GetFreeMagic(&num);
        if (!mp) continue;
        DF_MAGIC2_Init_MP(mp);
        mp->on = (short)num;
        mp->mdp = mdp;
        mp->comb = dp->comb;
        if (mdp->model_id) mp->tfm = Magic2_tfm[mdp->model_id];
        mp->flag = dp->option;
        if (dp->option & 0x80) {
            mp->ocmp = dp->ocmp;
            ObjCMatSt *oc = mp->ocmp;
            if (oc && oc->parentmatp) vu_mul_matrix(mp->matrix, (const float (*)[4])*oc->parentmatp, (const float (*)[4])oc->mat);
            else sceVu0CopyMatrix(mp->matrix, oc->mat);
            mp->mat0 = &mp->matrix;
        } else {
            mp->mat0 = dp->mat0;
            mp->oya = dp->oya;
        }
        if (dp->flag & 1) copy4(mp->loc, dp->pos);
        if (dp->flag & 2) copy4(mp->ang, dp->ang);
        copy4(mp->scale0, mdp->scale0);
        if (dp->flag & 4)
            for (int k = 0; k < 3; k++) mp->scale0[k] = mp->scale0[k] * dp->scl[k];
        if (dp->flag & 8) {
            for (int k = 0; k < 3; k++) mp->rgba0[k] = (unsigned char)f2u(dp->col[k]);
        } else if (dp->mdp->map_col) {
            for (int k = 0; k < 3; k++) mp->rgba0[k] = CopyOfSysInfo.colSfx.rgba[k];
        } else {
            for (int k = 0; k < 3; k++) mp->rgba0[k] = mdp->color0[k];
        }
        if (dp->flag & 0x10) mp->rgba0[3] = (unsigned char)f2u((float)mdp->color0[3] * dp->col[3]);
        else mp->rgba0[3] = mdp->color0[3];
        if (dp->onp) {
            mp->onp = dp->onp;
            if (*mp->onp < 0) *mp->onp = -2;
        }
        mp->onp = dp->onp;
        _magic2_st *oya = dp->oya;
        id_share(&mp->id, oya && oya->on > 0 ? oya->id : dp->id);
        if (mdp->half_ang_inv && (dp->n & 1) == (i & 1)) mp->flag |= 0x200;   /* alternate spin direction */
        DF_MAGIC2_Init(mp, 1);
    }
    dp->n--;
}

/* Start of an effect: random position / direction / speed / spin from its data, angles relative to the parent
 * matrix, speed vector; with oya (or flag 8) under mat0 the effect is made independent of mat0 (world space, new
 * id). Then the first key frame, texture animation, blur shake and the children at frame 1. */
void DF_MAGIC2_Init(_magic2_st *mp, int make_child)
{
    PS2_FRAME(0x100);
    _magic2_data_st *d = mp->mdp;
    int have_matrix = 0;
    float rp[4] __attribute__((aligned(16)));
    float ra[4] __attribute__((aligned(16)));
    if (!d->oya && mp->oya) mp->oya->flag |= 0x4000;
    for (int i = 0; i < 3; i++) rp[i] = d->rand_pos[i] * rnd_sym();
    rp[3] = 1.0f;
    for (int i = 0; i < 3; i++) ra[i] = d->rot[i] + d->rot2[i] * rnd_sym();
    ra[3] = 1.0f;
    for (int i = 0; i < 3; i++) mp->ang[i] = wrap_pi(mp->ang[i]);
    if (!mp->mat0) {
        char xyz = d->oya_xyz;
        if (xyz == 0) {
            ra[2] = ra[2] + mp->ang[2];
            ra[0] = ra[0] + mp->ang[0];
            ra[1] = ra[1] + mp->ang[1];
        } else if (xyz == 1) {
            ra[0] = ra[0] + mp->ang[0];
            ra[1] = ra[1] + mp->ang[1];
        }
    } else {
        for (int i = 0; i < 3; i++) ra[i] = ra[i] + mp->ang[i];
    }
    for (int i = 0; i < 3; i++) ra[i] = wrap_pi(ra[i]);

    float (*m)[4] = SPR_M(0x3000);
    if (!d->rotate) {
        sceVu0RotMatrixX(m, IdentityMtx, -ra[0]);
        sceVu0RotMatrixY(m, m, ra[1]);
        sceVu0RotMatrixZ(m, m, ra[2]);
    } else {
        sceVu0RotMatrixX(m, IdentityMtx, -ra[0]);
        sceVu0RotMatrixZ(m, m, ra[2]);
        sceVu0RotMatrixY(m, m, ra[1]);
    }
    mp->n_speed[0] = m[2][0];
    mp->n_speed[1] = m[2][1];
    mp->n_speed[2] = m[2][2];
    mp->n_speed[3] = 1.0f;

    unsigned int r = (unsigned int)ps2_rand() & 0xffff;
    mp->speed = d->speed + d->spd_rnd * ((float)(int)r / 65535.0f);
    for (int i = 0; i < 3; i++) mp->loc[i] = mp->loc[i] + rp[i];
    acvu0_ScaleVector(rp, mp->n_speed, d->r);
    for (int i = 0; i < 3; i++) mp->loc[i] = mp->loc[i] + rp[i];
    for (int i = 0; i < 3; i++) {
        _magic2_data_st *dd = mp->mdp;
        if (dd->ang_spd_rnd[i] == 0.0f) {
            mp->ang_spd_rnd[i] = dd->rot_spd[i];
        } else {
            float f = rnd_sym();
            dd = mp->mdp;
            mp->ang_spd_rnd[i] = dd->rot_spd[i] + dd->ang_spd_rnd[i] * f;
        }
    }
    mp->frame = d->endress ? -1 : (short)d->count;

    if (d->ang_opt == 1) {
        float a2 = rnd_sym() * d->ang_rnd[2] + d->ang_ofs[2];
        if (mp->mat0) a2 = a2 + mp->ang[2];
        else if (d->oya_xyz == 0) a2 = a2 + mp->ang[2];
        ra[2] = wrap_pi(a2);
        ra[0] = 0.0f;
        ra[1] = 0.0f;
    } else {
        float r0 = rnd_sym(), r1 = rnd_sym(), r2 = rnd_sym();
        float a0 = r0 * d->ang_rnd[0] + d->ang_ofs[0];
        float a1 = r1 * d->ang_rnd[1] + d->ang_ofs[1];
        float a2 = r2 * d->ang_rnd[2] + d->ang_ofs[2];
        if (d->ang_opt == 0) {
            if (!mp->mat0) {
                char xyz = d->oya_xyz;
                if (xyz == 0) {
                    a2 = a2 + mp->ang[2];
                    a0 = a0 + mp->ang[0];
                    a1 = a1 + mp->ang[1];
                } else if (xyz == 1) {
                    a0 = a0 + mp->ang[0];
                    a1 = a1 + mp->ang[1];
                }
            } else {
                a0 = a0 + mp->ang[0];
                a1 = a1 + mp->ang[1];
                a2 = a2 + mp->ang[2];
            }
        }
        ra[0] = wrap_pi(a0);
        ra[1] = wrap_pi(a1);
        ra[2] = wrap_pi(a2);
    }

    if (mp->mat0 && (d->oya || (mp->flag & 8))) {   /* independent of mat0 from now on */
        float m80[4][4] __attribute__((aligned(16)));
        float v[4] __attribute__((aligned(16)));
        float a[4] __attribute__((aligned(16)));
        char xyz = d->oya_xyz;
        if (xyz == 0 || xyz == 1) {
            if (xyz == 0) {
                sceVu0CopyMatrix(m80, *mp->mat0);
                m80[3][0] = 0.0f;
                m80[3][1] = 0.0f;
                m80[3][2] = 0.0f;
                m80[3][3] = 1.0f;
            } else {
                MatrixZtoAng(a, *mp->mat0);
                a[3] = 1.0f;
                a[2] = 0.0f;
                sceVu0RotMatrixZ(m80, IdentityMtx, 0.0f);
                sceVu0RotMatrixX(m80, m80, -a[0]);
                sceVu0RotMatrixY(m80, m80, a[1]);
            }
            vu_apply(v, (const float (*)[4])m80, mp->n_speed);
            sceVu0Normalize(mp->n_speed, v);
            if (d->ang_opt != 2) {
                MatrixZtoAng(mp->ang, m80);
                acvu0_AddVector(mp->ang, mp->ang, ra);
            } else {
                copy4(mp->ang, ra);
            }
        } else if (d->ang_opt != 2) {
            acvu0_AddVector(mp->ang, mp->ang, ra);
        } else {
            copy4(mp->ang, ra);
        }
        acvu0_ScaleVector(mp->rotspd, mp->n_speed, mp->speed);
        for (int i = 0; i < 3; i++) mp->ang[i] = wrap_pi(mp->ang[i]);
        vu_apply(mp->loc, (const float (*)[4])*mp->mat0, mp->loc);
        mp->oya = 0;
        mp->mat0 = 0;
        mp->flag &= 0xff70;
        df_magic2_ID_delete(&mp->id);
        id_new(&mp->id);
    } else {
        copy4(mp->ang, ra);
        acvu0_ScaleVector(mp->rotspd, mp->n_speed, mp->speed);
    }

    mp->anim_func = (char)mp->mdp->nkey;
    for (int i = 0; i < 3; i++) mp->scale[i] = ((float)mp->mdp->scl_key[0][i] * mp->scale0[i]) / 100.0f;
    mp->scale[3] = 1.0f;
    for (int i = 0; i < 4; i++) mp->rgba[i] = (float)(mp->rgba0[i] * mp->mdp->col_key[0][i]) / 100.0f;
    copy4(mp->rgbai[0], mp->rgba);
    copy4(mp->rgbai[1], mp->rgba);
    mp->ptime = d->pframe;
    if (d->nanim) {
        short tbp = (short)d->tbp;
        mp->compTBP = tbp;
        mp->drawTBP = tbp;
        mp->iniTBP = tbp;
        mp->addTBP = (short)d->add;
        mp->tawait = (unsigned char)d->wait;
        mp->lasTBP = (short)(mp->iniTBP + mp->addTBP * (d->nanim - 1));
        short cbp = (short)d->cbp;
        mp->drawCBP = cbp;
        mp->iniCBP = cbp;
    }
    if ((mp->flag & 0x20) && mp->mat0) copy4(mp->pos0, (*mp->mat0)[3]);
    else copy4(mp->pos0, mp->loc);
    if (d->bokashi) mp->bokashi_id = (char)DF_SHAKE_AppearShake(mp->loc, 0xffffffffu);

    if (make_child) {
        for (int i = 0; i < 3; i++) {
            if (d->child_mid[i] <= 0 || d->child_frm[i] != 1) continue;
            if (!have_matrix) {
                calc_matrix(mp);
                have_matrix = 1;
            }
            int res;
            if (mp->comb) {
                res = DF_MAGIC2_AppearMagic_InMagic2Ptr(comb_magic(mp->comb, d->child_mid[i]), mp->comb, 0, mp, 4);
            } else {
                unsigned short mid = (unsigned short)mp->mdp->child_mid[i];
                res = mid && mid < 0x200 ? DF_MAGIC2_AppearMagic_InMagic2Ptr(MagicData2 + mid, 0, 0, mp, 4) : 0;
            }
            if (res == -1) break;
        }
    }
    mp->flag |= 0x1000;
}

void DF_MAGIC2_Init_MP(_magic2_st *mp)
{
    if (!mp) return;
    ZeroMemory32b(mp, 0x68);
    mp->scale[3] = 1.0f;
    mp->scale[2] = 1.0f;
    mp->scale[1] = 1.0f;
    mp->scale[0] = 1.0f;
    mp->ang[3] = 1.0f;
    mp->loc[3] = 1.0f;
    mp->rgba[3] = 128.0f;
    mp->rgba[2] = 128.0f;
    mp->rgba[1] = 128.0f;
    mp->rgba[0] = 128.0f;
    mp->alpha_rate = 1.0f;
    mp->scale_rate = 1.0f;
    mp->cnt = -1;
    sceVu0CopyMatrix(mp->matrix, IdentityMtx);
    mp->id = 0;
}

void DF_MAGIC2_BoostColorChange(_magic2_st *mp, int *col)
{
    if (!mp || !col) return;
    for (int i = 0; i < 4; i++) mp->rgba0[i] = (unsigned char)col[i];
    _magic2_st *s = mp->sibling;
    if (s)
        for (int i = 0; i < 4; i++) s->rgba0[i] = (unsigned char)col[i];
}

/* one boost effect, started hidden (flag 0x10) at cnt 0 */
static _magic2_st *boost_new(_magic2_data_st *md, _stSfxCombHead *comb, ObjCMatSt *ocmp, float (*mat0)[4][4],
                             unsigned short option)
{
    int num;
    _magic2_st *mp = DF_MAGIC2_GetFreeMagic(&num);
    if (!mp) return 0;
    DF_MAGIC2_Init_MP(mp);
    mp->on = (short)num;
    mp->mdp = md;
    mp->comb = comb;
    if (md->model_id) mp->tfm = Magic2_tfm[md->model_id];
    mp->flag = option | 0x100;
    if (!ocmp) {
        mp->mat0 = mat0;
    } else {
        mp->ocmp = ocmp;
        if (ocmp->parentmatp) acvu0_MulMatrix(mp->matrix, *ocmp->parentmatp, ocmp->mat);
        else acvu0_CopyMatrix(mp->matrix, ocmp->mat);
        mp->mat0 = &mp->matrix;
        mp->flag |= 0x80;
    }
    acvu0_CopyVectorF(mp->scale0, md->scale0);
    for (int i = 0; i < 4; i++) mp->rgba0[i] = md->color0[i];
    DF_MAGIC2_Init(mp, 0);
    mp->flag |= 0x10;
    if (mp->bokashi_id) DF_SHAKE_SetSW(mp->bokashi_id, 0);
    mp->cnt = 0;
    return mp;
}

/* A boost effect plus its sibling (the next record with option 0x800, else the comb child at frame 1), the sibling
 * with the spin direction (0x200) inverted. */
_magic2_st *DF_MAGIC2_AppearBoostPtr(_magic2_data_st *mdp, _stSfxCombHead *comb, ObjCMatSt *ocmp, float (*mat0)[4][4],
                                     unsigned short option)
{
    PS2_FRAME(0x90);
    if (comb && comb_bad(mdp, comb)) return 0;
    if (!mdp) return 0;
    _magic2_st *mp = boost_new(mdp, comb, ocmp, mat0, option);
    if (!mp) return 0;
    if (option & 0x800) {
        mp->sibling = boost_new(mdp + 1, comb, ocmp, mat0, (unsigned short)(option ^ 0x200));
    } else if (comb && mdp->child_mid[0] && mdp->child_frm[0] == 1) {
        _magic2_data_st *md = comb_magic(comb, mdp->child_mid[0]);
        mp->sibling = boost_new(md, comb, ocmp, mat0, (unsigned short)(option ^ 0x200));
    }
    return mp;
}

_magic2_st *DF_MAGIC2_AppearBoost(unsigned short mid, ObjCMatSt *ocmp, float (*mat0)[4][4], unsigned short option)
{
    PS2_FRAME(0x10);
    if (!mid || mid >= 0x200) return 0;
    if (!mat0 && !ocmp) return 0;
    return DF_MAGIC2_AppearBoostPtr(MagicData2 + mid, 0, ocmp, mat0, option);
}

/* an effect started immediately (no duplicate), with its own id */
_magic2_st *DF_MAGIC2_AppearMagic2Ptr(_magic2_data_st *mdp, _stSfxCombHead *comb, float *pos, float *ang,
                                      unsigned short option)
{
    PS2_FRAME(0x80);
    if (comb && comb_bad(mdp, comb)) return 0;
    if (!mdp) return 0;
    int num;
    _magic2_st *mp = DF_MAGIC2_GetFreeMagic(&num);
    if (!mp) return 0;
    DF_MAGIC2_Init_MP(mp);
    id_new(&mp->id);
    mp->on = (short)num;
    mp->mdp = mdp;
    mp->comb = comb;
    if (mdp->model_id) mp->tfm = Magic2_tfm[mdp->model_id];
    mp->flag = option;
    if (pos) copy4(mp->loc, pos);
    if (ang) copy4(mp->ang, ang);
    mp->scale0[0] = mdp->scale0[0];
    mp->scale0[1] = mdp->scale0[1];
    mp->scale0[2] = mdp->scale0[2];
    for (int i = 0; i < 4; i++) mp->rgba0[i] = mdp->color0[i];
    DF_MAGIC2_Init(mp, 1);
    return mp;
}

_magic2_st *DF_MAGIC2_AppearMagic2(unsigned short mid, float *pos, float *ang, unsigned short option)
{
    PS2_FRAME(0x10);
    if (!mid || mid >= 0x200) return 0;
    return DF_MAGIC2_AppearMagic2Ptr(MagicData2 + mid, 0, pos, ang, option);
}

/* a child of oya: positioned by oya's matrix, sharing its id, optionally inheriting scale / colour */
int DF_MAGIC2_AppearMagic_InMagic2Ptr(_magic2_data_st *mdp, _stSfxCombHead *comb, float *pos, _magic2_st *oya,
                                      unsigned short option)
{
    float col[4] __attribute__((aligned(16)));
    if (comb && comb_bad(mdp, comb)) return 0;
    if (!mdp) return 0;
    _duplicate_st_ *dp = dup_alloc();
    if (!dp) return 0;
    if (oya) id_share(&dp->id, oya->id);
    else id_new(&dp->id);
    signed char cs = oya->mdp->child_scale, cr = oya->mdp->child_rgba;
    if (cr)
        for (int i = 0; i < 4; i++) col[i] = (float)oya->rgba0[i];
    if (pos) {
        dp->flag |= 1;
        copy4(dp->pos, pos);
    }
    if (cs) {
        dp->flag |= 4;
        copy4(dp->scl, oya->scale0);
    }
    if (cr) {
        dp->flag |= 8;
        copy4(dp->col, col);
    }
    dup_set_n(dp, mdp);
    dp->mat0 = &oya->matrix;
    dp->mdp = mdp;
    dp->comb = comb;
    dp->option = option;
    dp->oya = oya;
    return 0;
}

/* an effect attached to an object matrix (option 0x80); returns its id */
int DF_MAGIC2_AppearMagicOCMPPtr(_magic2_data_st *mdp, _stSfxCombHead *comb, ObjCMatSt *ocmp, unsigned short option)
{
    if (comb && comb_bad(mdp, comb)) return 0;
    if (!mdp) return 0;
    if ((option & 0x400) && !rand_pass(0x3d8)) return 0;
    if ((option & 0x8000) && !rand_pass(0x148)) return 0;
    _duplicate_st_ *dp = dup_alloc();
    if (!dp) return 0;
    id_new(&dp->id);
    dp->ocmp = ocmp;
    dup_set_n(dp, mdp);
    dp->mdp = mdp;
    dp->comb = comb;
    dp->option = option | 0x80;
    return dp->id;
}

int DF_MAGIC2_AppearMagicOCMP(unsigned short mid, ObjCMatSt *ocmp, unsigned short option)
{
    if (!mid || mid >= 0x200) return 0;
    if (!ocmp) return 0;
    return DF_MAGIC2_AppearMagicOCMPPtr(MagicData2 + mid, 0, ocmp, option);
}

int DF_MAGIC2_AppearMagic_SPL2AlphaPtr(_magic2_data_st *mdp, _stSfxCombHead *comb, float *pos, float *ang, float *scl,
                                       float *col, float (*mat0)[4][4], float (*n_speed)[4], unsigned short option,
                                       short *onp, float alpha)
{
    (void)n_speed;
    if (comb && comb_bad(mdp, comb)) return 0;
    if (!mdp) return 0;
    _duplicate_st_ *dp = dup_alloc();
    if (!dp) return 0;
    if ((option & 0x400) && !rand_pass(0x3d8)) return 0;   /* dp stays linked (quirk) */
    if ((option & 0x8000) && !rand_pass(0x148)) return 0;
    id_new(&dp->id);
    dup_set_vectors(dp, pos, ang, scl, col);
    dp->flag |= 0x10;
    dp->col[3] = alpha;
    dp->mat0 = mat0;
    dup_set_n(dp, mdp);
    dp->mdp = mdp;
    dp->comb = comb;
    dp->option = option;
    dp->onp = onp;
    return dp->id;
}

int DF_MAGIC2_AppearMagic_SPL2Alpha(unsigned short mid, float *pos, float *ang, float *scl, float *col,
                                    float (*mat0)[4][4], float (*n_speed)[4], unsigned short option, short *onp,
                                    float alpha)
{
    if (!mid || mid >= 0x200) return 0;
    return DF_MAGIC2_AppearMagic_SPL2AlphaPtr(MagicData2 + mid, 0, pos, ang, scl, col, mat0, n_speed, option, onp,
                                              alpha);
}

int DF_MAGIC2_AppearMagic_SPL2Ptr(_magic2_data_st *mdp, _stSfxCombHead *comb, float *pos, float *ang, float *scl,
                                  float *col, float (*mat0)[4][4], float (*n_speed)[4], unsigned short option,
                                  short *onp)
{
    (void)n_speed;
    if (comb && comb_bad(mdp, comb)) return 0;
    if (!mdp) return 0;
    _duplicate_st_ *dp = dup_alloc();
    if (!dp) return 0;
    if ((option & 0x400) && !rand_pass(0x3d8)) return 0;   /* dp stays linked (quirk) */
    if ((option & 0x8000) && !rand_pass(0x148)) return 0;
    id_new(&dp->id);
    dup_set_vectors(dp, pos, ang, scl, col);
    dp->mat0 = mat0;
    dup_set_n(dp, mdp);
    dp->mdp = mdp;
    dp->comb = comb;
    dp->option = option;
    dp->onp = onp;
    return dp->id;
}

int DF_MAGIC2_AppearMagic_SPL2(unsigned short mid, float *pos, float *ang, float *scl, float *col, float (*mat0)[4][4],
                               float (*n_speed)[4], unsigned short option, short *onp)
{
    PS2_FRAME(0x20);
    if (!mid || mid >= 0x200) return 0;
    return DF_MAGIC2_AppearMagic_SPL2Ptr(MagicData2 + mid, 0, pos, ang, scl, col, mat0, n_speed, option, onp);
}

/* returns the new id, -1 when mdp lies outside the comb's magic table */
int DF_MAGIC2_AppearMagicPtr(_magic2_data_st *mdp, _stSfxCombHead *comb, float *pos, float *ang, float *scl, float *col,
                             float (*mat0)[4][4], float (*n_speed)[4], unsigned short option)
{
    (void)n_speed;
    if (comb && comb_bad(mdp, comb)) return -1;
    if (!mdp) return 0;
    if ((option & 0x400) && !rand_pass(0x3d8)) return 0;
    if ((option & 0x8000) && !rand_pass(0x148)) return 0;
    _duplicate_st_ *dp = dup_alloc();
    if (!dp) return 0;
    id_new(&dp->id);
    dup_set_vectors(dp, pos, ang, scl, col);
    dp->mat0 = mat0;
    dup_set_n(dp, mdp);
    dp->mdp = mdp;
    dp->comb = comb;
    dp->option = option;
    return dp->id;
}

#ifndef ACNB_SOURCEPORT
/* MIPS -> native wrappers (gen_bridge cannot wrap the stack arguments): a0..t2 = mdp .. mat0, t3 = n_speed (unused,
 * dropped from DWARF), option = u16 in the caller's 8-byte stack slot sp+0, onp at sp+8 */
static void bw__DF_MAGIC2_AppearMagic_SPL2Ptr(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    _magic2_data_st *mdp = (_magic2_data_st *)bridge_ptr(c->gpr[A0].uw[0]);
    _stSfxCombHead *comb = (_stSfxCombHead *)bridge_ptr(c->gpr[A1].uw[0]);
    float *pos = (float *)bridge_ptr(c->gpr[A2].uw[0]);
    float *ang = (float *)bridge_ptr(c->gpr[A3].uw[0]);
    float *scl = (float *)bridge_ptr(c->gpr[T0].uw[0]);
    float *col = (float *)bridge_ptr(c->gpr[T1].uw[0]);
    float (*mat0)[4][4] = (float (*)[4][4])bridge_ptr(c->gpr[T2].uw[0]);
    float (*n_speed)[4] = (float (*)[4])bridge_ptr(c->gpr[T3].uw[0]);
    unsigned short option = (unsigned short)rd32(sp);
    short *onp = (short *)bridge_ptr(rd32(sp + 8));
    int r = DF_MAGIC2_AppearMagic_SPL2Ptr(mdp, comb, pos, ang, scl, col, mat0, n_speed, option, onp);
    c->gpr[V0].sd[0] = r;
}

/* a0..t2 = mid .. n_speed, t3 = option, onp in the caller's 8-byte stack slot sp+0 (not the SPL2Ptr layout) */
static void bw__DF_MAGIC2_AppearMagic_SPL2(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    unsigned short mid = (unsigned short)c->gpr[A0].uw[0];
    float *pos = (float *)bridge_ptr(c->gpr[A1].uw[0]);
    float *ang = (float *)bridge_ptr(c->gpr[A2].uw[0]);
    float *scl = (float *)bridge_ptr(c->gpr[A3].uw[0]);
    float *col = (float *)bridge_ptr(c->gpr[T0].uw[0]);
    float (*mat0)[4][4] = (float (*)[4][4])bridge_ptr(c->gpr[T1].uw[0]);
    float (*n_speed)[4] = (float (*)[4])bridge_ptr(c->gpr[T2].uw[0]);
    unsigned short option = (unsigned short)c->gpr[T3].uw[0];
    short *onp = (short *)bridge_ptr(rd32(sp));
    int r = DF_MAGIC2_AppearMagic_SPL2(mid, pos, ang, scl, col, mat0, n_speed, option, onp);
    c->gpr[V0].sd[0] = r;
}

/* same layout without onp */
static void bw__DF_MAGIC2_AppearMagicPtr(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    _magic2_data_st *mdp = (_magic2_data_st *)bridge_ptr(c->gpr[A0].uw[0]);
    _stSfxCombHead *comb = (_stSfxCombHead *)bridge_ptr(c->gpr[A1].uw[0]);
    float *pos = (float *)bridge_ptr(c->gpr[A2].uw[0]);
    float *ang = (float *)bridge_ptr(c->gpr[A3].uw[0]);
    float *scl = (float *)bridge_ptr(c->gpr[T0].uw[0]);
    float *col = (float *)bridge_ptr(c->gpr[T1].uw[0]);
    float (*mat0)[4][4] = (float (*)[4][4])bridge_ptr(c->gpr[T2].uw[0]);
    float (*n_speed)[4] = (float (*)[4])bridge_ptr(c->gpr[T3].uw[0]);
    unsigned short option = (unsigned short)rd32(sp);
    int r = DF_MAGIC2_AppearMagicPtr(mdp, comb, pos, ang, scl, col, mat0, n_speed, option);
    c->gpr[V0].sd[0] = r;
}
#endif

int DF_MAGIC2_AppearMagic(unsigned short mid, float *pos, float *ang, float *scl, float *col, float (*mat0)[4][4],
                          float (*n_speed)[4], unsigned short option)
{
    if (!mid || mid >= 0x200) return 0;
    return DF_MAGIC2_AppearMagicPtr(MagicData2 + mid, 0, pos, ang, scl, col, mat0, n_speed, option);
}

/* DF_GetFreeBuffer callback: a slot is free when on == 0 (slot 0 never) */
int DF_MAGIC2_CheckFree(void *adr)
{
    if (((_magic2_st *)adr)->on) return 0;
    return adr != Mag2St;
}

_magic2_st *DF_MAGIC2_GetFreeMagic(int *num)
{
    PS2_FRAME(0x20);
    _magic2_st *mp = (_magic2_st *)DF_GetFreeBuffer(&s_MagTop, Mag2St, 0x1a0, 0x1ff, GFN(DF_MAGIC2_CheckFree));
    if (!mp) return 0;
    *num = s_MagTop;
    s_MagTop = s_MagTop + 1;
    if (!(s_MagTop < 0x200)) s_MagTop = 1;
    return mp;
}

/* alpha_rate of every effect with this id, then of their siblings' ids */
void DF_MAGIC2_SetAlphaRate(int id, float rate)
{
    if (!id_live(id)) return;
    _magic2_st *mp = Mag2St + 1;
    for (int i = 1; i < 0x200; i++, mp++) {
        if (mp->id != id) continue;
        mp->alpha_rate = rate;
        if (mp->sibling) DF_MAGIC2_SetAlphaRate(mp->sibling->id, rate);
    }
}

void DF_MAGIC2_SetScaleRate(int id, float rate)
{
    if (!id_live(id)) return;
    _magic2_st *mp = Mag2St + 1;
    for (int i = 1; i < 0x200; i++, mp++) {
        if (mp->id != id) continue;
        mp->scale_rate = rate;
        if (mp->sibling) DF_MAGIC2_SetScaleRate(mp->sibling->id, rate);
    }
}

/* ---------------------------------------------------------------- removal */

/* kill every effect and pending duplicate with this id */
void DF_MAGIC2_Kill_ID(int id)
{
    PS2_FRAME(0x20);
    if (!id_live(id)) return;
    DF_MAGIC2_Kill_ID2(id);
    _duplicate_st_ *dp = s_Duplicate;
    for (int i = 0; i < 0x80; i++, dp++) {
        if (dp->id != id) continue;
        dup_unlink(dp);
        dp->n = 0;
        df_magic2_ID_delete(&dp->id);
    }
}

static void DF_MAGIC2_Kill_ID2(int id)
{
    PS2_FRAME(0x40);
    if (!id_live(id)) return;
    _magic2_st *mp = Mag2St + 1;
    for (int i = 1; i < 0x200; i++, mp++) {
        if (mp->id != id) continue;
        if (mp->on > 0) DF_MAGIC2_Kill(mp);
        else df_magic2_ID_delete(&mp->id);
    }
}

/* stretch effects (flag 0x20) shrink back over the remaining distance at oya_speed */
static void stretch_out_frames(_magic2_st *mp, const float *dv)
{
    float dist = ps2_sqrt((dv[0] * dv[0] + dv[1] * dv[1]) + dv[2] * dv[2]);
    float lim = mp->mdp->param[1];
    float r = dist < lim ? ps2_div(dist, mp->oya_speed) : ps2_div(lim, mp->oya_speed);
    if (r < 0.0f) r = -r;
    mp->frame = (short)ps2_ftoi(r);
    if ((float)mp->frame < r) mp->frame++;
    mp->oya_speed = ps2_div(mp->scale[2], (float)mp->frame);
}

/* detach from mat0: speed direction, position and angles into world space */
static void detach_mat0(_magic2_st *mp)
{
    float *t = SPR_F(0);
    acvu0_ApplyMatrix(t, *mp->mat0, mp->n_speed);
    acvu0_SubVector(t, t, (*mp->mat0)[3]);
    acvu0_Normalize(mp->n_speed, t);
    acvu0_ScaleVector(mp->rotspd, mp->n_speed, mp->speed);
    acvu0_ApplyMatrix(mp->loc, *mp->mat0, mp->loc);
    if (mp->mdp->oya_xyz == 2) {
        t[2] = 0.0f;
        t[1] = 0.0f;
        t[0] = 0.0f;
    } else {
        DF_MAGIC2_Mat2Ang(t, mp->mat0);
        t[0] = -t[0];
        t[1] = t[1] + k_pi;
        if (mp->mdp->oya_xyz == 1) t[2] = 0.0f;
    }
    t[3] = 1.0f;
    acvu0_AddVector(mp->ang, mp->ang, t);
    for (int i = 0; i < 3; i++) mp->ang[i] = wrap_pi(mp->ang[i]);
    mp->mat0 = 0;
}

/* on = -2 (fading out next frame), sibling and (child_kill) the whole id family too; a stretch effect detaches */
void DF_MAGIC2_Kill(_magic2_st *mp)
{
    PS2_FRAME(0x60);
    if (!mp || mp->on <= 0) return;
    mp->on = -2;
    if (mp->bokashi_id) {
        DF_SHAKE_Kill(mp->bokashi_id);
        mp->bokashi_id = 0;
    }
    _magic2_st *s = mp->sibling;
    if (s) {
        if (s->on > 0) {
            s->on = -2;
            if (s->bokashi_id) {
                DF_SHAKE_Kill(s->bokashi_id);
                s->bokashi_id = 0;
            }
            if (s->sibling) {
                DF_MAGIC2_Kill(s->sibling);
                s->sibling = 0;
            }
            if (s->mdp->child_kill) DF_MAGIC2_Kill_ID(s->id);
            DF_MAGIC2_Kill_sub(s);
            df_magic2_ID_delete(&s->id);
        }
        mp->sibling = 0;
    }
    if (mp->mdp->child_kill) {
        unsigned short id = mp->id;
        if (id_live(id)) {
            _magic2_st *p = Mag2St + 1;
            for (int i = 1; i < 0x200; i++, p++) {
                if (p->id != id) continue;
                if (p->on <= 0) df_magic2_ID_delete(&p->id);
                else DF_MAGIC2_Kill(p);
            }
            _duplicate_st_ *dp = s_Duplicate;
            for (int i = 0; i < 0x80; i++, dp++) {
                if (dp->id != id) continue;
                dup_unlink(dp);
                dp->n = 0;
                df_magic2_ID_delete(&dp->id);
            }
        }
    }
    if (mp->flag & 0x20) {
        if (mp->oya_speed == 0.0f) {
            mp->frame = 0;
        } else {
            float dv[4] __attribute__((aligned(16)));
            acvu0_SubVector(dv, mp->pos0, (*mp->mat0)[3]);
            stretch_out_frames(mp, dv);
        }
        DF_MAGIC2_INDEPENDENCE(mp);
    }
    df_magic2_ID_delete(&mp->id);
}

void DF_MAGIC2_INDEPENDENCE(_magic2_st *mp)
{
    PS2_FRAME(0x30);
    mp->oya = 0;
    if (mp->mat0) detach_mat0(mp);
}

/* the sibling part of DF_MAGIC2_Kill: stretch effects detach */
void DF_MAGIC2_Kill_sub(_magic2_st *mp)
{
    PS2_FRAME(0x40);
    if (!(mp->flag & 0x20)) return;
    if (mp->oya_speed == 0.0f) {
        mp->frame = 0;
    } else {
        float dv[4] __attribute__((aligned(16)));
        acvu0_SubVector(dv, mp->pos0, (*mp->mat0)[3]);
        stretch_out_frames(mp, dv);
    }
    mp->oya = 0;
    if (mp->mat0) detach_mat0(mp);
}

void DF_MAGIC2_Mat2Ang(float *ang, void *mat)
{
    PS2_FRAME(0x20);
    MatrixZtoAng(ang, mat);
    ang[3] = 1.0f;
}

/* ---------------------------------------------------------------- setup */

void DF_MAGIC2_ReadTFM(void)
{
    PS2_FRAME(0x10);
    DF_MAGIC2_InitTFM(SysData_ReadMagicModel(), 0x200);
}

/* model table: offsets in the file -> pointers (model, its bounding box, each batch's packet and texture) */
static void DF_MAGIC2_InitTFM(void *pTfm, int tfm_num)
{
    if (!(tfm_num < 0x201)) tfm_num = 0x200;
    const int *ofs = (const int *)pTfm;
    for (int i = 0; i < tfm_num; i++) {
        Magic2_tfm[i] = 0;
        if (!ofs[i]) continue;
        Magic2_tfm[i] = (char *)pTfm + ofs[i];
        int *t = (int *)Magic2_tfm[i];
        int base = (int)PS2PTR(t);
        if (t[3]) t[3] = base + t[3];
        int *b = t + 4;
        for (unsigned int k = 0; k < (unsigned int)t[1]; k++, b += 4) {
            b[0] = b[0] + base;
            if (b[2]) b[2] = b[2] + base;
        }
    }
}

void DF_MAGIC2_AllInit(int reduce)
{
    PS2_FRAME(0x20);
    SysData_ReadMagicTMX();
    SysData_LoadMagicData(reduce);
    Magic2_Reduce = (unsigned char)reduce;
    s_MagTop = 0;
    NowFrameMag2Num = 0;
    Mag2St = (_magic2_st *)Memm_Alloc(1, 0x34000, 0x10);
    ZeroMemory32b(Mag2St, 0xd000);
    Magic2Matrix[0][0] = (float (*)[4][4])Memm_Alloc(1, 0x8000, 0x10);
    Magic2Matrix[0][1] = (float (*)[4][4])Memm_Alloc(1, 0x8000, 0x10);
    Magic2Matrix[1][0] = (float (*)[4][4])Memm_Alloc(1, 0x8000, 0x10);
    Magic2Matrix[1][1] = (float (*)[4][4])Memm_Alloc(1, 0x8000, 0x10);
    Magic2MatrixSW = 0;
    Magic2MatrixSC = 0;
    Magic2RgbaiSW = 0;
    NowFrameMagic2Matrix = Magic2Matrix[0][0];
    ZeroMemory32b(s_Duplicate, 0x1000);
    duplicate_top = 0;
    duplicate_now = 0;
    for (int i = 0; i < 0x200; i++) Mag2St[i].id = 0;
    ZeroMemory32b(StillLiveID, 0x100);
    for (int i = 0; i < 0x80; i++) s_Duplicate[i].id = 0;
    if (reduce) {
        g_distMagCutStart = 12.5f;
        g_distMagCutCmplt = 11.5f;
    } else {
        g_distMagCutStart = 11.0f;
        g_distMagCutCmplt = 9.0f;
    }
    s_rateMagCutAlpha = 1.0f / (g_distMagCutStart - g_distMagCutCmplt);
}
