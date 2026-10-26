/* src/sfx/meye.cpp - "monoeye" light trails (the glowing streak behind a mono-eye sensor). Four manager slots
 * (emng[4]): eye2Free chains the unused ones, eye2List the live ones. Each live slot keeps a 4-entry history of
 * its object's world position and XZY angles (pos[] / ang[] HermKeySt; only vec[] is used as the history ring),
 * shifted once per frame by DF_EYE2_Proc; DF_EYE2_MkDL / DF_EYE2_MakePacket (VIF1-chain GIF packet builders) draw
 * the ribbon from it: the trail model (TP2) placed at each history sample, alpha fading with age.
 * Render tags (MkDL): the eye texture upload = UPLOAD, objid RQ_OC_TEXTURE / tbp, sub PSMT4 (texmng convention);
 * the ribbon packet = EFFECT, objid RQ_OC_EFFECT / (0xe2 << 16 | emng slot), sub viewport. */
#include "acnb/port.h"
#include "acnb/vu0m.h"
#include "acnb/gif.h"
#include "acnb/sdk.h"

/* Shifts every live eye's history and appends the current world location / angles of its ObjCMatSt. */
void DF_EYE2_Proc(void)
{
    PS2_FRAME(0x80);
    PS2_STACK(float[4], pos, 0x20);
    PS2_STACK(float[4], ang, 0x30);
    /* Only written when the ObjCMatSt has a parent matrix: otherwise the original decomposes whatever is left in
     * this stack slot (original bug, kept). */
    PS2_STACK(float[4][4], m, 0x40);
    for (_StEye2Mng *e = eye2List; e; e = e->next) {
        ObjCMatSt *oc = e->ocmp;
        float (*pm)[4][4] = oc->parentmatp;
        if (pm) {
            float r[4][4];
            vu_mul_matrix(r, *pm, oc->mat);
            for (int i = 0; i < 4; i++) {
                PS2_VF_MIRROR(1 + i, (*pm)[i]);
                PS2_VF_MIRROR(22 + i, r[i]);
            }
            memcpy(m, r, sizeof r);
        }
        DF_GetSRTXZY(m, pos, ang, 0);
        for (int i = 3; i != 0; i--) {
            memcpy(e->pos[i].vec, e->pos[i - 1].vec, 16);
            memcpy(e->ang[i].vec, e->ang[i - 1].vec, 16);
        }
        memcpy(e->pos[0].vec, pos, 16);
        memcpy(e->ang[0].vec, ang, 16);
        if ((int)e->num < (int)e->max_num) e->num++;
    }
}

/* No range check on id. */
void DF_EYE2_ChangeColor(char id, int (*color)[4])
{
    memcpy(emng[(int)id].color, color, 0x10);
}

/* Takes a free slot for the trail model tp2p / tm2p on ocmp; returns the slot index or -1. */
char DF_EYE2_AppearEye(void *tp2p, void *tm2p, StModel *mp, ObjectSt *op, ObjCMatSt *ocmp, int (*color)[4])
{
    PS2_FRAME(0x30);
    _StEye2Mng *e = eye2Free;
    if (!e) return -1;
    eye2Free = e->next;
    e->ocmp = ocmp;
    e->pTP2 = (struct_9c961104 *)tp2p;
    e->pTM2 = (struct_c53f69de *)tm2p;
    e->mp = mp;
    e->op = op;
    e->next = 0;
    e->num = 0;
    memcpy(e->color, color, 0x10);
    for (int k = 0; k < 4; k++) {
        HermKeySt *p = &e->pos[k], *a = &e->ang[k];
        p->frame = 100 * k;
        a->frame = 100 * k;
        p->tens = 1.0f;
        a->tens = 1.0f;
        p->cont = 0.0f;
        a->cont = 0.0f;
        p->bias = 0.0f;
        a->bias = 0.0f;
        p->linear = 7;
        a->linear = 7;
        if (k == 0) {
            p->prev = 0;
            p->next = &e->pos[k + 1];
            a->prev = 0;
            a->next = &e->ang[k + 1];
        } else if (k == 3) {
            p->prev = &e->pos[k - 1];
            p->next = 0;
            a->prev = &e->ang[k - 1];
            a->next = 0;
        } else {
            p->prev = &e->pos[k - 1];
            p->next = &e->pos[k + 1];
            a->prev = &e->ang[k - 1];
            a->next = &e->ang[k + 1];
        }
    }
    /* Relocate the TP2 object offsets. Original bug (kept): the entry pointer is never advanced, so the first
     * entry's three offsets get the base added nobj times. */
    unsigned int *ent = (unsigned int *)((char *)e->pTP2 + 0x10);
    unsigned int first = 0;
    for (unsigned int i = 0; i < e->pTP2->nobj; i++) {
        ent[0x34 / 4] += (unsigned int)(uintptr_t)e->pTP2;
        ent[0x3c / 4] += (unsigned int)(uintptr_t)e->pTP2;
        ent[0x44 / 4] += (unsigned int)(uintptr_t)e->pTP2;
        if (first == 0) first = ent[0x34 / 4];
    }
    if (first) {
        memcpy(&e->tex0, (char *)(uintptr_t)first + 8, 8);
        memcpy(&e->tex1, (char *)(uintptr_t)first + 0x18, 8);
    }
    if (!eye2List) {
        eye2List = e;
        e->prev = 0;
    } else {
        _StEye2Mng *t = eye2List;
        while (t->next) t = t->next;
        t->next = e;
        e->prev = t;
    }
    return (char)((unsigned int)((char *)e - (char *)emng) / 0x240);
}

/* Clears the four slots and chains them into the free list; num = trail history length (max_num). */
void DF_EYE2_Init(int num)
{
    PS2_FRAME(0x50);
    eye2List = 0;
    eye2Free = &emng[0];
    _StEye2Mng *prev = 0;
    for (int i = 0; i < 4; i++) {
        _StEye2Mng *e = &emng[i];
        memset(e, 0, 0x240);
        e->max_num = (unsigned char)num;
        if (prev) prev->next = e;
        prev = e;
    }
}

/* The ribbon of one eye: header (DMA CNT, VIF DIRECT, A+D TEXFLUSH, additive ALPHA, CLAMP, TEST without Z write, a
 * dummy TEX0 that forces the CLUT reload, the eye's TEX0 / TEX1), then per history sample ii (thinned: samples
 * closer than 0.05 to the last drawn one are skipped) the TP2 model transformed by WORLD_SCREEN * (rotation of the
 * current angles, translated to the sample): one TRISTRIP GIF tag per primitive, ST / RGBAQ / XYZF2 per vertex. The
 * tags are written with EOP 0; the last one written gets EOP, the DMA / VIF counts are patched in at the end.
 * ADC: a vertex is not kicked when one of the two PREVIOUS vertices was off the guard band (the current vertex's
 * own flag only acts one and two vertices later; original behaviour, kept). */
static void DF_EYE2_MakePacket(unsigned int **gpp, _StEye2Mng *emngp)
{
    PS2_FRAME(0x1c0);
    /* pos.w is never written (MotionStep's linear path writes xyz) and flows into old_pos / vf16.w */
    PS2_STACK(float[4], pos, 0x110);
    PS2_STACK(float[4], old_pos, 0x1b0);
    float ang[4] __attribute__((aligned(16)));
    float eye_mat_0[4][4] __attribute__((aligned(16)));
    float eye_mat[4][4] __attribute__((aligned(16)));
    float stq[4] __attribute__((aligned(16)));
    int ivec[4] __attribute__((aligned(16)));

    unsigned int *p = *gpp;
    unsigned int *defgp = p;
    p = dma_tag(p, 0, GIF_ID_CNT, 0);                 /* qwc patched at the end */
    unsigned int *vifgp = p;
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 0; p += 4;   /* VIF DIRECT, patched at the end */
    unsigned int *gif = p;
    p = gif_tag(p, 7, 0, 0, 0, 0, 1, GIF_R_AD);
    p = put_ad(p, 0, 0x3f);                           /* TEXFLUSH */
    p = put_ad(p, 0x48, GS_ALPHA_1);                  /* Cs * As + Cd */
    p = put_ad(p, 5, GS_CLAMP_1);
    p = put_ad(p, 0x51001, 0x47);                     /* TEST_1: ATST NEVER, FB_ONLY, ZTST GEQUAL */
    p = put_ad(p, 0x8000000001400000ull, GS_TEX0_1);  /* dummy PSMT4, CLD 4 */
    p = put_ad(p, emngp->tex0, GS_TEX0_1);
    p = put_ad(p, emngp->tex1, GS_TEX1_1);

    DF_CALC_MotionStep(ang, &emngp->ang[0], 0);
    sceVu0RotMatrixX(eye_mat_0, IdentityMtx, ang[0]);
    sceVu0RotMatrixZ(eye_mat_0, eye_mat_0, ang[2]);
    sceVu0RotMatrixY(eye_mat_0, eye_mat_0, ang[1]);

    for (int ii = 0; ii < emngp->num; ii++) {
        float alpha = (float)((int)emngp->max_num - ii) / (float)emngp->max_num;
        if (ii == 0) {
            DF_CALC_MotionStep(pos, &emngp->pos[0], 0);
        } else {
            int frame = ps2_ftoi(200.0f * (float)ii / (float)emngp->max_num);
            HermKeySt *k = &emngp->pos[0];
            for (; k->next; k = k->next)
                if (frame >= k->frame && frame < k->next->frame) break;
            DF_CALC_MotionStep(pos, k, frame);
            float tpos[4] = {old_pos[0] - pos[0], old_pos[1] - pos[1], old_pos[2] - pos[2], old_pos[3]};
            PS2_VF_MIRROR(16, tpos);
            PS2_VF_MIRROR(17, pos);
            float len = ps2_sqrt(tpos[0] * tpos[0] + tpos[1] * tpos[1] + tpos[2] * tpos[2]);
            if (len <= 0.05f) continue;   /* old_pos stays at the last drawn sample */
            alpha *= 0.8f;
        }
        sceVu0TransMatrix(eye_mat, eye_mat_0, pos);
        {
            /* eye_mat = WORLD_SCREEN * eye_mat; the VU0 keeps it in vf1-vf4 for gfxvu0_RotTransPers (both builds) */
            float acc[4];
            for (int i = 0; i < 4; i++) {
                float a = WORLD_SCREEN_MATRIX[0][i] * eye_mat[3][0];
                a = a + WORLD_SCREEN_MATRIX[1][i] * eye_mat[3][1];
                a = a + WORLD_SCREEN_MATRIX[2][i] * eye_mat[3][2];
                acc[i] = a;
            }
            vu_mul_matrix(eye_mat, WORLD_SCREEN_MATRIX, eye_mat);
            auto *vu = VU0();
            for (int i = 0; i < 4; i++) {
                memcpy(vu->vf[1 + i].f, eye_mat[i], 16);
                memcpy(vu->vf[22 + i].f, eye_mat[i], 16);
            }
            memcpy(vu->acc, acc, 16);
        }
        _objtbl *otp = (_objtbl *)((unsigned char *)emngp->pTP2 + 0x10);
        for (unsigned int jj = 0; jj < emngp->pTP2->nobj; jj++, otp++) {
            struct_de4d68f5 *opp = otp->php;
            for (int kk = 0; kk < otp->p_num; kk++) {
                char clip = 0;
                struct_39fa86c6 *primp = (struct_39fa86c6 *)(opp + 1);
                gif = p;
                p = gif_tag(p, opp->pv_num & 0x7fff, 0, 1, 0x5c, 0, 3, 0x412);   /* TRISTRIP IIP TME ABE; ST RGBAQ XYZF2 */
                for (int v = 0; v < opp->pv_num; v++, primp++) {
                    gfxvu0_RotTransPers(ivec, stq, otp->vp[primp->vert_id]);
                    clip = (char)(clip >> 1);
                    float q = stq[3];
                    stq[0] = primp->s * q;
                    stq[1] = primp->t * q;
                    stq[2] = q;
                    p[0] = ps2_f2u(stq[0]); p[1] = ps2_f2u(stq[1]); p[2] = ps2_f2u(stq[2]); p[3] = 0;
                    p += 4;
                    p = put_rgbaq(p, (unsigned int)emngp->color[0], (unsigned int)emngp->color[1],
                                  (unsigned int)emngp->color[2],
                                  (unsigned int)ps2_ftoi(alpha * (alpha * ((float)emngp->color[3] * alpha))));
                    memcpy(p, ivec, 16);
                    p += 4;
                    if (!(ivec[0] >= 0x4000 && ivec[0] < 0xc001 && ivec[1] >= 0x4000 && ivec[1] < 0xc001 && ivec[2] > 0))
                        clip = (char)((clip & 3) | 4);
                    if (v >= 2 && (clip & 3)) p[-1] |= 0x8000;   /* ADC */
                }
                opp = (struct_de4d68f5 *)primp;
            }
        }
        memcpy(old_pos, pos, 16);
        if (ps2_ftoi((float)emngp->color[3] * alpha) == 0) break;
    }
    *gpp = p;
    gif[0] |= 0x8000;   /* EOP on the last GIF tag */
    defgp[0] |= (unsigned int)((int)((char *)p - (char *)defgp) / 16 - 1);
    vifgp[3] = (unsigned int)((int)((char *)p - (char *)vifgp) / 16 - 1) | 0x51000000;
}

/* Draws every live eye into the viewport's list: skipped for camera >= 4 outside PolyG play, per eye when its model
 * is unused / its object hidden or hidden in this viewport. Uploads the eye texture + CLUT, then the ribbon. */
void DF_EYE2_MkDL(unsigned int **gpp, int viewid)
{
    PS2_FRAME(0x50);
    unsigned int *gp = *gpp;
    if (!g_sortie.pwork->isPlayPolyg && (int)((char *)CUR_DRAW_CAM - (char *)CAM) / 0x100 >= 4) return;
    for (_StEye2Mng *e = eye2List; e; e = e->next) {
        unsigned char *tm2 = (unsigned char *)e->pTM2;
        if (!e->mp || (e->mp->use & 1)) continue;
        ObjectSt *op = e->op;
        if (!op || op->onoff == 1 || ((op->o_hide >> (viewid & 31)) & 1)) continue;
        if (tm2) {
            unsigned long long t0;
            memcpy(&t0, tm2 + 0x10, 8);   /* TEX0 */
            int tbp = *(unsigned short *)(tm2 + 0x10) & 0x3fff;
            ps2_rq_tag_packet(gp, RQ_K_UPLOAD, RQ_OBJID(RQ_OC_TEXTURE, tbp), 0x14 /* PSMT4 */);
            GL_LoadImage4b(&gp, tm2 + 0x20, tbp, 1 << ((tm2[0x13] >> 2) & 0xf), 1 << ((t0 >> 30) & 0xf),
                           tm2 + 0x20 + *(int *)(tm2 + 4), (int)((t0 >> 37) & 0x3fff));
        }
        if (e->pTP2) {
            ps2_rq_tag_packet(gp, RQ_K_EFFECT, RQ_OBJID(RQ_OC_EFFECT, 0xe2u << 16 | (unsigned int)(e - emng)), viewid);
            DF_EYE2_MakePacket(&gp, e);
        }
    }
    *gpp = gp;
    ps2_rq_tag_packet(gp, RQ_K_NONE, 0, 0);
}
