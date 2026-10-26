/* src/gfx/shdow.cpp â€” drop shadows of ACs and characters. Each AC shadow is its body + 8 part models re-drawn from the
 * light's point of view into a 256x256 texture (lightscreen matrix), which is then projected onto the ground models
 * around the AC (DF_MkDL_SMapModel). Characters (Chr, stage NPCs) get the same with one model each. The whole file
 * is native: the per-model DMA chains (matrix REF + model CALL), the in-place recolouring of the shadow models' VU1
 * packets, the light matrices and the per-frame pass. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
int drawmode = 1;   /* 0x27ad9c */
#endif

/* The 8 part models drawn into an AC's shadow, in the original order, and their shadow copies. */
static StModel *shadow_part(int k, int i, int shadow)
{
    StModel *const tbl[8][2] = {{MyRHW, MyRHWS}, {MyLHW, MyLHWS}, {MyRBW, MyRBWS}, {MyLBW, MyLBWS},
                                {MyREX, MyREXS}, {MyLEX, MyLEXS}, {MySLG, MySLGS}, {MyEOB, MyEOBS}};
    return &(tbl[k][shadow])[i];
}

/* Light view (eye = AC centre + light direction * s_light_dist) and lightscreen = shadow_screen * lightview for
 * player's AC, then the shadow copies take the models' current positions (the shadow is one frame behind). */
static void update_acshadow_matrix(int player, float srange)
{
    PS2_FRAME(0xa0);
    const float half = 0.5f, deg = ps2_u2f(0x3c8efa35), min_elv = ps2_u2f(0x393702d3);
    sceVu0ViewScreenMatrix(shadow_screen_matrix, ps2_div(256.0f * (half * s_light_dist), srange), 1.0f, 1.0f, 2048.0f,
                           2048.0f, 0.0f, ps2_u2f(0x4b7fffff), 1.0f, 65536.0f);

    StModel *ms = &MyACS[player];
    float at[4] __attribute__((aligned(16)));
    memcpy(at, ms->loc, 16);
    at[1] = at[1] - half * MyAC[player].tall;
    float elv = deg * light_elv, dir = deg * light_dir;
    if (elv < min_elv) elv = min_elv;
    float dist = s_light_dist;
    float v[4] __attribute__((aligned(16)));
    float se = fsmath_SinVU0(elv);   /* (the calls in the original order) */
    float sd = fsmath_SinVU0(dir);
    v[0] = se * sd;
    v[1] = -fsmath_CosVU0(elv);
    se = fsmath_SinVU0(elv);
    float cd = fsmath_CosVU0(dir);
    v[2] = se * cd;
    v[3] = 1.0f;
    float sc[4] = {v[0] * dist, v[1] * dist, v[2] * dist, v[3]};   /* vmulx.xyz */
    float eye[4] = {at[0] + sc[0], at[1] + sc[1], at[2] + sc[2], at[3]};
    memcpy(&VU0()->vf[17], sc, 16);
    memcpy(&VU0()->vf[16], eye, 16);
    gluLookAtF(lightview_matrix, eye[0], eye[1], eye[2], at[0], at[1], at[2], 0.0f, 1.0f, 0.0f);

    vu_mul_matrix(lightscreen_matrix, (const float (*)[4])shadow_screen_matrix, (const float (*)[4])lightview_matrix);
    memcpy(&VU0()->vf[1], shadow_screen_matrix, 64);
    memcpy(&VU0()->vf[22], lightscreen_matrix, 64);

    memcpy(ms->loc, MyAC[player].loc, 16);
    for (int k = 0; k < 7; k++) memcpy(shadow_part(k, player, 1)->loc, shadow_part(k, player, 0)->loc, 16);
}

/* Character variant of update_acshadow_matrix: the light looks at the shadow copy's position (lowered by half the
 * model's height), then the shadow copy takes the model's position. */
static void update_chrshadow_matrix(StModel *mp, StModel *mps, float srange)
{
    PS2_FRAME(0x90);
    const float half = 0.5f, deg = ps2_u2f(0x3c8efa35), min_elv = ps2_u2f(0x393702d3);
    sceVu0ViewScreenMatrix(shadow_screen_matrix, ps2_div(256.0f * (half * s_light_dist), srange), 1.0f, 1.0f, 2048.0f,
                           2048.0f, 0.0f, ps2_u2f(0x4b7fffff), 1.0f, 65536.0f);

    float at[4] __attribute__((aligned(16)));
    memcpy(at, mps->loc, 16);
    at[1] = at[1] - half * mp->tall;
    float elv = deg * light_elv, dir = deg * light_dir;
    if (elv < min_elv) elv = min_elv;
    float dist = s_light_dist;
    float v[4] __attribute__((aligned(16)));
    float se = fsmath_SinVU0(elv);   /* (the calls in the original order) */
    float sd = fsmath_SinVU0(dir);
    v[0] = se * sd;
    v[1] = -fsmath_CosVU0(elv);
    se = fsmath_SinVU0(elv);
    float cd = fsmath_CosVU0(dir);
    v[2] = se * cd;
    v[3] = 1.0f;
    float sc[4] = {v[0] * dist, v[1] * dist, v[2] * dist, v[3]};   /* vmulx.xyz */
    float eye[4] = {at[0] + sc[0], at[1] + sc[1], at[2] + sc[2], at[3]};
    memcpy(&VU0()->vf[17], sc, 16);
    memcpy(&VU0()->vf[16], eye, 16);
    gluLookAtF(lightview_matrix, eye[0], eye[1], eye[2], at[0], at[1], at[2], 0.0f, 1.0f, 0.0f);

    vu_mul_matrix(lightscreen_matrix, (const float (*)[4])shadow_screen_matrix, (const float (*)[4])lightview_matrix);
    memcpy(&VU0()->vf[1], shadow_screen_matrix, 64);
    memcpy(&VU0()->vf[22], lightscreen_matrix, 64);

    memcpy(mps->loc, mp->loc, 16);
}

/* One part's DMA chain: CNT (VIF UNPACK of 4 qw at VU1 addr 4) + REF of the matrix + CALL into the model's packet. */
static unsigned int *put_shadow_part(unsigned int *q, unsigned int mat, unsigned int mstart)
{
    q[0] = 0x10000001; q[1] = 0; q[2] = 0; q[3] = 0;
    q[4] = 0; q[5] = 0; q[6] = 0; q[7] = 0x6c040004;
    q[8] = 0x30000004; q[9] = mat; q[10] = 0; q[11] = 0;
    q[12] = 0x50000000; q[13] = mstart; q[14] = 0; q[15] = 0;
    return q + 16;
}

/* Per-frame chain of one model's parts (shown parts only; animated TOD models skip parts without a model). The
 * matrix is the model's current read matrix, the packet the shadow copy's. */
static void mkdl_shadow_model2(unsigned int **gpp, StModel *mp, StModel *mps)
{
    unsigned int *g = *gpp;
    for (int i = 0; i < mp->nobj; i++) {
        ObjectSt *op = &mp->op[i];
        if (op->onoff) continue;
        int idx;
        if (mp->kind & 1) {
            idx = mp->tod->animtable[mp->tod->nowtodid][i].tp2id;
            if (idx == 0xff) continue;
        } else {
            idx = i;
        }
        g = put_shadow_part(g, PS2PTR(op->rdmatp), mps->omdtable[idx].mstart);
    }
    *gpp = g;
}

/* Sortie-start chain of one model's parts: hidden parts take mkdl_effect_vu1_const as matrix, shown ones the shadow
 * copy's write matrix (the shadow is drawn one frame behind). */
static void mkdl_shadow_model(unsigned int **gpp, StModel *mp, StModel *mps)
{
    unsigned int *g = *gpp;
    for (int i = 0; i < mp->nobj; i++) {
        int idx = mp->tod->animtable[mp->tod->nowtodid][i].tp2id;
        if (idx == 0xff) continue;
        unsigned int mat = mp->op[i].onoff ? PS2PTR(mkdl_effect_vu1_const) : PS2PTR(mps->op[i].wrmatp);
        g = put_shadow_part(g, mat, mps->omdtable[idx].mstart);
    }
    *gpp = g;
}

/* Recolour a shadow model's VU1 packets in place: per block, clear 8 bits of header word 0x21, then every vertex
 * with alpha <= 254.9 gets the shadow colour and every opaque one is pushed far away (the data convention), and the
 * block's end word becomes the first / next-block marker. */
static void model_for_shadow(StModel *mp, unsigned int *vu1mp, OmdTableSt *omdtable)
{
    const float c255[4] = {255.0f, 255.0f, 255.0f, 128.0f};
    const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float farv[4] = {99999.0f, 99999.0f, 99999.0f, 1.0f};
    const float lim = ps2_u2f(0x437ee666);   /* 254.9 */
    unsigned int *p = vu1mp;
    for (int i = 0; i < mp->nmobj; i++) {
        int first = 1;
        unsigned int n = omdtable[i].pnum;
        do {   /* (pnum 0 would wrap: never in the data) */
            int cnt = (int)((p[0] & 0x0fffffffu) - 10u) / 4;
            p[0x21] &= 0xfc03ffffu;
            p += 0x28;
            for (unsigned int k = (unsigned int)cnt; k != 0; k--, p += 0x10) {
                struct_00faa976 *r = (struct_00faa976 *)p;
                if (r->rgba[3] <= lim) {
                    memcpy(r->rgba, c255, 16);
                } else {   /* (NaN too: c.ole) */
                    memcpy(r->rgba, zero, 16);
                    memcpy(r->vertex, farv, 16);
                }
            }
            p[3] = first ? 0x15000600u : 0x17000000u;
            first = 0;
            p += 4;
        } while (--n);
    }
}

/* A character's shadow display list: lightscreen matrix REF + its parts' chain, END. */
static unsigned int *create_model_pkt(StModel *mp, StModel *mps)
{
    PS2_FRAME(0x40);
    model_for_shadow(mps, mps->vu1mp, mps->omdtable);
    unsigned int *buf = (unsigned int *)Memm_Alloc(1, (mp->nobj * 4 + 4) * 16, 0x40); /* + debug file / line */
    unsigned int *q = buf;
    q[0] = 0x10000001; q[1] = 0; q[2] = 0; q[3] = 0;
    q[4] = 0; q[5] = 0; q[6] = 0; q[7] = 0x6c040000;
    q[8] = 0x30000004; q[9] = PS2PTR(lightscreen_matrix); q[10] = 0; q[11] = 0;
    unsigned int *p = q + 12;
    mkdl_shadow_model(&p, mp, mps);
    GL_EndList(&p);
    return buf;
}

/* One model's shadow: render it (AC: body + parts) from the light into the shadow texture, then project that onto the
 * ground models in a cylinder below it. Alpha fades out with the height above the ground (vanish_ht). */
static void mkdl_shadow(unsigned int **gpp, StModel *mp, int view)
{
    PS2_FRAME(0x390);
    /* locals at their original stack slots: GL_LoadMatrix2(.., 0x4a, loc) reads 48 bytes past loc (the hit list) */
    struct Locals { float loc2[4]; int clc[4]; int scr[4]; float loc[4]; StModel *hits[131]; int nhit; };
    static_assert(offsetof(Locals, nhit) == 0x38c - 0x140, "mkdl_shadow frame");
    PS2_STACK(Locals, fr, 0x140);
    int *clc = fr.clc;
    int *scr = fr.scr;
    float *loc = fr.loc;
    StModel **hits = fr.hits;
    int *nhit = &fr.nhit;

    memcpy(loc, mp->loc, 16);
    loc[1] = mp->LocY;
    float h = mp->LocY - mp->loc[1];
    int a;
    if (0.0f < vanish_ht) {
        float t = h < 0.0f ? 0.0f : h <= vanish_ht ? h : vanish_ht;
        a = ps2_ftoi(max_alpha * (1.0f - ps2_div(t, vanish_ht)));
    } else {
        a = ps2_ftoi(max_alpha);
    }
    rgbashadow[3] = ps2_ftoi((float)a);
    if (!rgbashadow[3]) return;
    DF_CALC_ScreenPointEx(scr, current_ws_matrix, loc);
    if (scr[0] < 0x6000 || scr[0] >= 0xe001 || scr[1] < 0x6000 || scr[1] >= 0xe001) return;
    if (mp->type != 4 && mp->type != 3) return;

    unsigned int *g = *gpp;
    memset(clc, 0, 16);
    GL_Sync(&g);
    GL_SetRegister(&g, 0x3f, 0);
    GL_SetDrawEnv1(&g, 0x100, 0x100, 2, 0x3b40, clc);
    GL_SetRegister(&g, 0x47, 0x30000);
    GL_SetRegister(&g, 0x4e, 0x131000000ULL);
    if (mp->type == 3) {
        update_chrshadow_matrix(mp, ChrS + mp->aid, shadow_range_chr);
        s_range[3] = ps2_div(0.5f, shadow_range_chr);
        GL_LoadMatrix2(&g, 0, lightscreen_matrix);
        mkdl_shadow_model2(&g, mp, ChrS + mp->aid);
    } else {
        int id = mp->aid;
        update_acshadow_matrix(id, shadow_range);
        s_range[3] = ps2_div(0.5f, shadow_range);
        GL_LoadMatrix2(&g, 0, lightscreen_matrix);
        mkdl_shadow_model2(&g, mp, &MyACS[id]);
        for (int k = 0; k < 8; k++) {
            StModel *pm = shadow_part(k, id, 0);
            if (!(pm->use & 0x80)) mkdl_shadow_model2(&g, pm, shadow_part(k, id, 1));
        }
    }
    *nhit = 0;
    HitBoxGetInCylinderModel(loc, shadow_width, 12.0f, 0x10002, 1, nhit, hits, 0x80);
    if (drawmode) {
        DF_MkDL_ResetScreen(&g, view, 1);
        GL_LoadMatrix2(&g, 0, current_ws_matrix);
        GL_LoadMatrix2(&g, 0xc, lightview_matrix);
        GL_LoadMatrix2(&g, 0x4a, loc);
        GL_LoadVector2(&g, 0x4c, s_range);
        GL_LoadVector2(&g, 0x1b, rgbashadow);
    } else {
        DF_MkDL_ResetScreen(&g, view, 0);
    }
    GL_SetRegister(&g, 0x3f, 0);
    GL_SetRegister(&g, 6, 0x8000000620213b40ULL);
    GL_SetRegister(&g, 0x14, 0x60);
    GL_SetRegister(&g, 8, 5);
    GL_SetRegister(&g, 0x47, 0x51001);
    if (shadow_alpha == 2) GL_SetRegister(&g, 0x42, 0x42);
    else if (shadow_alpha == 1) GL_SetRegister(&g, 0x42, 0x48);
    else if (shadow_alpha == 0) GL_SetRegister(&g, 0x42, 0x44);
    if (drawmode) {
        GL_SetRegister(&g, 0x1a, 0);
        GL_SetRegister(&g, 0x1b, 0x50);
    }
    float *loc2 = fr.loc2;   /* (dead copy in the original, kept for the stack contents) */
    memcpy(loc2, mp->loc, 16);
    loc2[1] = mp->LocY;
    for (int i = 0; i < *nhit; i++)
        if (drawmode) DF_MkDL_SMapModel(&g, hits[i]);
    if (drawmode) DF_MkDL_ResetScreen(&g, view, 0);
    *gpp = g;
}

/* Shadow pass of one viewport: every AC (users even when destroyed) and every character on a visible layer. */
void MkDL_ACShadows(unsigned int **gpp, int view)
{
    PS2_FRAME(0x70);
    if (view < 0 || drawoff) return;
    float vh = -1.0f;
    if (light_elv != 0.0f) vh = ps2_div(17.0f, ps2_tanf(ps2_u2f(0x3c8efa35) * light_elv));
    vanish_ht = vh;
    unsigned int *g = *gpp;
    DF_VP_GetMatrix(view, current_ws_matrix, 0, 0);
    ps2_rq_tag_packet(g, RQ_K_SHADOW, 0, 0xffff);
    GL_Sync(&g);
    GL_SetRegister(&g, 0x3b, 0x8000008080ULL);
    GL_SetRegister(&g, 0x42, 0x42);
    GL_SetRegister(&g, 0x47, 0x5000f);
    if (drawmode == 1) {
        GL_Sync(&g);
        g[0] = 0x50000000;
        g[1] = (unsigned int)(uintptr_t)g_pmpgVu1ShadowMap;
        g[2] = 0;
        g[3] = 0;
        g += 4;
        GL_Sync(&g);
        s_range[1] = 12.0f;
        s_range[0] = shadow_width;
        s_range[2] = shadow_width;
        s_range[3] = ps2_div(0.5f, shadow_range);
    }
    for (int i = 0; i < NowPlayerNum; i++) {
        StModel *m = &MyAC[i];
        if ((i < NowUserPlayerNum || !g_ac[i].dead) && !(m->use & 1) && !(m->use & 0x80)) {
            ps2_rq_tag_packet(g, RQ_K_SHADOW, RQ_OBJID(RQ_OC_AC_PART, (unsigned int)m->aid << 8), i);
            mkdl_shadow(&g, m, view);
        }
    }
    StModel *c = Chr;
    for (int i = 0; i < StgInfo.cnum; i++, c++)
        if (!(c->use & 1) && !(c->use & 0x80) && (c->on_layer & 1)) {
            ps2_rq_tag_packet(g, RQ_K_SHADOW, RQ_OBJID(RQ_OC_CHR, PS2PTR(c)), i);
            mkdl_shadow(&g, c, view);
        }
    if (drawmode) {
        GL_Sync(&g);
        g[0] = 0x50000000;
        g[1] = (unsigned int)(uintptr_t)g_pmpgVu12f;
        g[2] = 0;
        g[3] = 0;
        g += 4;
        GL_Sync(&g);
        DF_MkDL_ResetScreen(&g, view, 0);
    }
    *gpp = g;
    ps2_rq_tag_packet(g, RQ_K_NONE, 0, 0);   /* end of the shadow packets */
}

/* Sortie start: one display list per AC that renders its body + parts into the shadow texture (DMA: call
 * lightscreen_matrix by reference, then the shadow models), and the packets of the stage's characters. */
void DF_InitACShadows(void)
{
    PS2_FRAME(0x190);
    for (int i = 0; i < 4; i++) ACShadowKickP[i] = 0;
    for (int i = 0; i < NowPlayerNum; i++) {
        unsigned int *p = (unsigned int *)Memm_Alloc(1, 0x11c0, 0x40); /* + debug file / line, ignored */
        ACShadowKickP[i] = (char *)p;
        static const unsigned int head[12] = {0x10000001, 0, 0, 0, 0, 0, 0, 0x6c040000, 0x30000004, 0x2c4780, 0, 0};
        memcpy(p, head, sizeof head);
        p += 12;
        mkdl_shadow_model(&p, &MyAC[i], &MyACS[i]);
        StModel *s = &MyACS[i];
        model_for_shadow(s, s->vu1mp, s->omdtable);
        for (int k = 0; k < 8; k++) {
            StModel *ps = shadow_part(k, i, 1);
            mkdl_shadow_model(&p, shadow_part(k, i, 0), ps);
            model_for_shadow(ps, ps->vu1mp, ps->omdtable);
        }
        GL_EndList(&p);
    }
    for (int i = 0; i < 0x400; i++) chrshadowkickp[i] = 0;
    for (int i = 0; i < StgInfo.cnum; i++) chrshadowkickp[i] = (char *)create_model_pkt(Chr + i, ChrS + i);
}

void DF_InitChrShadows(void)
{
    ChrShadowKickP = 0;
}

void DF_SetShadowOff(int off)
{
    drawoff = off;
}

void DF_SetShadowAngle(float angy, float angv)
{
    light_dir = angy;
    light_elv = angv;
}
