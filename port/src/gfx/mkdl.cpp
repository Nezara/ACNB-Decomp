/* src/gfx/mkdl.cpp — per-frame 3D display list of the battle (DF_MkDL_Frame into DLBuffer[BuffID & 1]): background
 * models, then per viewport the general list (visibility test + z sort of map / object / character / AC / item models,
 * model packets, AC shadows, "replaced opacity" models), then effects (magic2 matrices, sorted translucent draw
 * callbacks, magic3, eye). A model packet = light / fog / alpha registers for the VU1 program + per object its matrix
 * (UNPACK of a DMA ref) and a DMA call of its OMD geometry; objects crossing the frustum sides or near plane go
 * through DF_MkDL_ClipModel once per plane when StgConf.nearclip is on. */
#include "acnb/sdk.h"
#include "acnb/vu0m.h"
#include "acnb/nodwarf.h"
#include "acnb/hooks.h"

#ifdef ACNB_SOURCEPORT
float __SubMakeReplacedOpcDl__vAlpba[4] = {0, 0, 0, 1.0f};   /* function static, 0x25c040 */
#endif

#define AC_PART(arr, mp) (&(arr)[(mp)->aid])
#define XLU_REG mkdl_xlu_reg

static inline void put64(unsigned int *p, unsigned long long v) { p[0] = (unsigned int)v; p[1] = (unsigned int)(v >> 32); }

/* DMA call tag (id 5) to addr */
static inline unsigned int *put_call(unsigned int *p, unsigned int addr)
{
    p[0] = 0x50000000; p[1] = addr; p[2] = 0; p[3] = 0;
    return p + 4;
}

/* DMA cnt 1 + UNPACK V4-32 x4 to VU vuaddr, then DMA ref 4 qw -> addr (GL_LoadMatrices with one matrix) */
static inline unsigned int *put_matrix_ref(unsigned int *p, unsigned int vuaddr, unsigned int addr)
{
    p[0] = 0x10000001; p[1] = 0; p[2] = 0; p[3] = 0;
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0x6c040000 | vuaddr;
    p[8] = 0x30000004; p[9] = addr; p[10] = 0; p[11] = 0;
    return p + 12;
}
#define put_obj_matrix(p, addr) put_matrix_ref(p, 4, addr)   /* object matrix to VU 4 */

/* The VU0 macro-mode transform of the original: ((m0 * x + m1 * y) + m2 * z) + m3 * 1 (vmaddw with vf0w). */
static inline void xform1(float *out, const float (*m)[4], const float *v)
{
    float x = v[0], y = v[1], z = v[2];
    for (int i = 0; i < 4; i++) {
        float a = m[0][i] * x;
        a = a + m[1][i] * y;
        a = a + m[2][i] * z;
        out[i] = a + m[3][i] * 1.0f;
    }
}

/* lqc2 vf1..vf4 <- m: interpreted callees read the matrix from there */
static inline void vu0_load_vf1_4(const float (*m)[4]) { memcpy(&VU0()->vf[1], m, 64); }

int GetPlayerViewID(int player)
{
    for (int i = 0; i < ViewNum; i++)
        if ((&ViewCamID[0])[i] == player) return i;
    return -1;
}

void DF_MkDL_SetVFCFactor(float x, float y)
{
    float bx = ps2_sqrt(1.0f + x * x), by = ps2_sqrt(1.0f + y * y);
    s_aax = x;
    s_aay = y;
    s_bbx = bx;
    s_bby = by;
}

static void __DrawModelArray(unsigned int **gpp, StModel **model_p, int model_num, int view)
{
    PS2_FRAME(0x80);
    unsigned int *g = *gpp;
    for (int i = 0; i < model_num; i++) {
        StModel *mp = model_p[i];
        switch (mp->type) {
        case 1:
            if (!(mp->mflag & 0x20000)) __DrawModel(&g, mp, view);
            break;
        case 2: {
            __DrawModel(&g, mp, view);
            unsigned short lnk = mp->array->lnkid;
            if (lnk && !(mp->use & 4)) __DrawModel(&g, Map + lnk, view);
            break;
        }
        case 3: case 4: case 6: case 7: case 8:
            __DrawModel(&g, mp, view);
            break;
        }
    }
    *gpp = g;
}

/* AC body + parts: core, arms, legs, booster, extension... MyRHW / MyLHW only when the arm part is a weapon arm */
static inline void ac_part_tag(unsigned int *g, StModel *mp, unsigned int part)
{
    ps2_rq_tag_packet(g, RQ_K_MODEL, RQ_OBJID(RQ_OC_AC_PART, (unsigned int)mp->aid << 8 | part), part);
}

static void ac_models(unsigned int **gpp, StModel *mp, unsigned char dflag, int hide)
{
    ac_part_tag(*gpp, mp, 0);
    DF_MkDL_Model(gpp, mp, dflag, hide);
    ac_part_tag(*gpp, mp, 1);
    DF_MkDL_Model(gpp, AC_PART(MyRCV, mp), dflag, hide);
    ac_part_tag(*gpp, mp, 2);
    DF_MkDL_Model(gpp, AC_PART(MyLCV, mp), dflag, hide);
    ac_part_tag(*gpp, mp, 3);
    DF_MkDL_Model(gpp, AC_PART(MyBST, mp), dflag, hide);
    ac_part_tag(*gpp, mp, 4);
    DF_MkDL_Model(gpp, AC_PART(MyEOB, mp), dflag, hide);
    ac_part_tag(*gpp, mp, 5);
    DF_MkDL_Model(gpp, AC_PART(MySLG, mp), dflag, hide);
    if (((AcPlayerSt *)mp->arg0p)->param.armsp->type) {
        ac_part_tag(*gpp, mp, 6);
        DF_MkDL_Model(gpp, AC_PART(MyRHW, mp), dflag, hide);
        ac_part_tag(*gpp, mp, 7);
        DF_MkDL_Model(gpp, AC_PART(MyLHW, mp), dflag, hide);
    }
}

static void __DrawModelReplacedOpc(unsigned int **gpp, StModel *mp, int view)
{
    PS2_FRAME(0x60);
    unsigned int *g = *gpp;
    /* DMA cnt 3: GIF A+D TEST_1 = 0x5180b (no VIF code) */
    put64(g + 0, 0x10000003); put64(g + 2, 0);
    put64(g + 4, 0x1000000000008002ULL); put64(g + 6, 0xe);
    put64(g + 8, 0x44); put64(g + 10, 0x42);
    put64(g + 12, 0x5180b); put64(g + 14, 0x47);
    g += 16;
    unsigned char dflag = (mp->mflag & 0x200) != 0;
    if (mp->type == 4) {
        unsigned int *g2 = g;
        ac_models(&g2, mp, dflag, 1 << (view & 31));
        g = g2;
    } else {
        DF_MkDL_Model(&g, mp, dflag, 1 << (view & 31));
    }
    *gpp = g;
}

/* AC texture swap: when the original packs a DMA call to address 0 it is skipped */
static inline unsigned int *ac_tex_call(unsigned int *p, unsigned int addr) { return addr ? put_call(p, addr) : p; }

/* render tag of one model's packets: AC (type 4 / 7) by AC id, characters (3) and stage / other models by address */
static void model_tag(unsigned int *g, StModel *mp)
{
    unsigned long long id;
    if (mp->type == 4 || mp->type == 7) id = RQ_OBJID(RQ_OC_AC_PART, (unsigned int)mp->aid << 8);
    else if (mp->type == 3) id = RQ_OBJID(RQ_OC_CHR, PS2PTR(mp));
    else id = RQ_OBJID(RQ_OC_STAGE_MESH, PS2PTR(mp));
    ps2_rq_tag_packet(g, RQ_K_MODEL, id, mp->type);
}

static void __DrawModel(unsigned int **gpp, StModel *mp, int view)
{
    PS2_FRAME(0x140);
    unsigned int *g = *gpp;
    model_tag(g, mp);
    int pass, npass;
    if (mp->mflag & 0x40) { pass = 1; npass = 2; }
    else if (mp->mflag & 0x200) { pass = 0; npass = 2; }
    else { pass = 0; npass = 1; }

    _map_model_list_st *ml;
    if (mp->type == 3 || mp->type == 7 || mp->type == 4) {
        float lm[4][4] __attribute__((aligned(16))), cm[4][4] __attribute__((aligned(16)));
        if (mp->type == 3) {
            ml = mp->nowparent->mlist;
            if (!ml) ml = Map->mlist;
        } else if (!mp->nowparent || !(ml = mp->nowparent->mlist)) {
            ml = Map->mlist;
        }
        GL_SetRegister(&g, 0x43, (unsigned long long)mp->alpha2 << 32 | mp->blend2);
        DF_MakeModelLightMatrix_Chr(mp, lm, cm);
        GL_LoadMatrix2(&g, 0xc, lm);
        GL_LoadMatrix2(&g, 0x10, cm);
    } else {
        ml = mp->mlist;
        if (!ml) ml = Map->mlist;
        GL_LoadMatrices(&g, 0xc, ml->lp_light, 2);
    }

    /* DMA cnt 10 + UNPACK 9 qw to VU 0x40: vuparam, 4 point light positions, 4 colours (unused slots not written) */
    g[0] = 0x1000000a; g[1] = 0; g[2] = 0; g[3] = 0; g[4] = 0; g[5] = 0; g[6] = 0; g[7] = 0x6c090040;
    memcpy(g + 8, &mp->vuparam, 16);
    unsigned int *pos = g + 12, *col = g + 28;
    const unsigned char *plid = mp->plid;
    for (int n = mp->vuparam.plightnum; n != 0; n--, plid++) {
        memcpy(pos, PLight[*plid].loc, 16); pos += 4;
        memcpy(col, PLight[*plid].col, 16); col += 4;
    }
    g = col + (4 - mp->vuparam.plightnum) * 4;

    if (pass >= npass) { *gpp = g; ps2_rq_tag_packet(g, RQ_K_NONE, 0, 0); return; }
    int hide = 1 << (view & 31);
    for (; pass < npass; pass++) {
        unsigned char dflag;
        _map_fog_st *fog = ml->lp_fog;
        /* DMA cnt 6 + UNPACK 5 qw to VU 0x14: fog params, then GIF A+D FOGCOL / ALPHA_1 / TEST_1 */
        g[0] = 0x10000006; g[1] = 0; g[2] = 0; g[3] = 0; g[4] = 0; g[5] = 0; g[6] = 0; g[7] = 0x6c050014;
        memcpy(g + 8, fog->fog, 16);
        put64(g + 12, 0x1000000000008003ULL); put64(g + 14, 0xe);
        put64(g + 16, (unsigned long long)((long long)fog->fog_color[2] << 16 | (long long)fog->fog_color[0] |
                                           (long long)fog->fog_color[1] << 8));
        put64(g + 18, 0x3d);
        if (pass == 1) {
            put64(g + 20, AlphaBlendPaturn[mp->array->alpha]); put64(g + 22, 0x42);
            put64(g + 24, (mp->mflag & 0x20000) ? 0x4180b : 0x5180b); put64(g + 26, 0x47);
            dflag = (mp->mflag & 0x200) ? 1 : 0;
        } else {
            put64(g + 20, 0xa); put64(g + 22, 0x42);
            put64(g + 24, 0x7000f); put64(g + 26, 0x47);
            dflag = 0;
        }
        g += 28;

        if (mp->type == 4 || mp->type == 7) {   /* AC textures: per part swaps, or the whole AC texture set */
            int sw = mp->on >> 10;
            unsigned int aid = mp->aid;
            if (AcOpt.txx) {
                if (CurACTex != (int)mp->aid) {
                    GL_LoadImage(&g, (char *)g_pactex[mp->aid] + 0x40000, 0x3d40, 0x40, 0x20);
                    CurACTex = mp->aid;
                }
                g = ac_tex_call(g, PS2PTR(g_pactex[mp->aid]) + sw * 0xd0 + 0x46080);
                if (sw == 0 || sw == 5 || sw == 6) g = ac_tex_call(g, PS2PTR(g_pactex[mp->aid]) + 0x467d0);
                GL_SetRegister(&g, 0x3f, 0);
                GL_SetRegister(&g, 6, 0x8000000001400000ULL);
            } else {
                int r = g_isSwTexTransR[aid] && sw == 1;
                int l = g_isSwTexTransL[aid] && sw == 2;
                if (r || l) {
                    GL_LoadImage(&g, (char *)g_pactex[aid] + 0x40000, (aid & 0xffff) * 0x20 + 0x3d40, 0x40, 0x20);
                    if (r) {
                        g = ac_tex_call(g, PS2PTR(g_pactex[mp->aid]) + 0x46150);
                        g_isSwTexTransR[mp->aid] = 0;
                    }
                    if (l) {
                        g = ac_tex_call(g, PS2PTR(g_pactex[mp->aid]) + 0x46220);
                        g_isSwTexTransL[mp->aid] = 0;
                    }
                }
            }
        }
        if (mp->type == 4) {
            unsigned int *g2 = g;
            ac_models(&g2, mp, dflag, hide);
            g = g2;
        } else {
            DF_MkDL_Model(&g, mp, dflag, hide);
        }
    }
    *gpp = g;
    ps2_rq_tag_packet(g, RQ_K_NONE, 0, 0);   /* end of this model's packets */
}

void DF_MkDL_ACModel(unsigned int **gpp, StModel *mp, unsigned char dflag, int hide)
{
    PS2_FRAME(0x60);
    unsigned int *g = *gpp;
    ac_models(&g, mp, dflag, hide);
    *gpp = g;
}

static OmdTableSt *call_omd_switch(StModel *mp)
{
    return (OmdTableSt *)HOSTPTR(gcall(omd_switch, mp));
}

/* index of object i's geometry in the OMD table: the animated object table remaps (0xff = not drawn) */
static inline int obj_geom(StModel *mp, int i)
{
    if (!(mp->kind & 1)) return i;
    StXtod *tod = mp->tod;
    return ((unsigned char *)tod->animtable[tod->nowtodid])[i * 0x30 + 2];
}

void DF_MkDL_SMapModel(unsigned int **gpp, StModel *mp)
{
    PS2_FRAME(0xd0);
    unsigned int *g = *gpp;
    OmdTableSt *omd = call_omd_switch(mp);
    if (!omd || !mp->nobj) return;
    for (int i = 0; i < mp->nobj; i++) {
        ObjectSt *op = &mp->op[i];
        if (op->onoff) continue;
        int id = obj_geom(mp, i);
        if (id == 0xff) continue;
        float inv[4][4] __attribute__((aligned(16)));
        sceVu0InversMatrix(inv, *op->wrmatp);
        GL_LoadMatrix2(&g, 0x10, inv);
        g = put_obj_matrix(g, PS2PTR(mp->op[i].rdmatp));
        g = put_call(g, omd[id].mstart);
    }
    *gpp = g;
}

void DF_MkDL_Model(unsigned int **gpp, StModel *mp, unsigned char dflag, int hide)
{
    PS2_FRAME(0x200);
    unsigned int *g = *gpp;
    OmdTableSt *omd = call_omd_switch(mp);
    if (!omd || !mp->nobj) return;
    for (int i = 0; i < mp->nobj; i++) {
        ObjectSt *op = &mp->op[i];
        if (op->onoff || (hide & op->o_hide) || dflag != op->dflag) continue;
        int id = obj_geom(mp, i);
        if (id == 0xff) continue;
        g = put_obj_matrix(g, PS2PTR(op->rdmatp));
        if (!StgConf.clip_only && (!StgConf.nearclip || (mp->mflag & 0x800) || !(mp->clip & 0x3f)))
            g = put_call(g, omd[id].mstart);
        if (!StgConf.nearclip) continue;
        unsigned char clip = mp->clip;
        if (!(clip & 0x3f) || (mp->mflag & 0x800)) continue;

        /* clip planes in object space: m = CAMERA_MATRIX1 * object matrix (VU0 vf1-4 x vf22-25) */
        float (*om)[4] = *mp->op[i].wrmatp;
        float m[4][4] __attribute__((aligned(16))), t[4][4] __attribute__((aligned(16)));
        float p[4] __attribute__((aligned(16))), pln[5][4] __attribute__((aligned(16)));
        vu0_load_vf1_4(CAMERA_MATRIX1);
        vu_mul_matrix(m, (const float (*)[4])CAMERA_MATRIX1, (const float (*)[4])om);
        memcpy(&VU0()->vf[22], m, 64);
        DFvu0_TransposeMatrix(t, m);
        acvu0_ApplyMatrix(p, t, m[3]);
        p[3] = 1.0f;
        int np = 0;
        for (int k = 0; k < 5; k++, clip >>= 1) {
            if (!(clip & 1)) continue;
            float *pl = pln[np];
            float c, s;
            switch (k) {
            case 0: case 1:   /* left / right: cos(fovx) * column x +- ..., sin(fovx) * column z */
                c = k == 0 ? s_cos_fovx : -s_cos_fovx; s = s_sin_fovx;
                for (int r = 0; r < 3; r++) pl[r] = c * m[r][0] + s * m[r][2];
                break;
            case 2: case 3:   /* top / bottom */
                c = k == 2 ? s_cos_fovy : -s_cos_fovy; s = s_sin_fovy;
                for (int r = 0; r < 3; r++) pl[r] = c * m[r][1] + s * m[r][2];
                break;
            default:          /* near plane, 5 units in front of the camera */
                pl[0] = m[0][2]; pl[1] = m[1][2]; pl[2] = m[2][2];
                pl[3] = m[3][2] - 5.0f;
                break;
            }
            if (k < 4) pl[3] = (p[0] * pl[0] + p[1] * pl[1]) + p[2] * pl[2];
            acvu0_Normalize_xyz(pl, pl);
            np++;
        }
        unsigned int *src = (unsigned int *)HOSTPTR(omd[id].mstart);
        int pnum = omd[id].pnum;
        for (int k = 0; k < np && pnum > 0; k++) {
            int last = k == np - 1;
            unsigned int *dst = last ? g : clip_buff[k & 1], *out = dst;
            pnum = DF_MkDL_ClipModel(&out, src, pnum, pln[k], last);
            if (last) g = out;
            src = dst;
        }
    }
    *gpp = g;
}

static OmdTableSt *omd_switch_plnum(StModel *mp)
{
    if ((unsigned int)(mp->type - 1) < 2 || mp->type == 8)
        return mp->vuparam.plightnum > 0 ? mp->omdtable : mp->omdtablelw;
    return mp->omdtable;
}

static OmdTableSt *omd_switch_dist(StModel *mp)
{
    if ((unsigned int)(mp->type - 1) < 2 || mp->type == 8)
        return (mp->use & 0x1000) ? mp->omdtablelw : mp->omdtable;
    return mp->omdtable;
}

/* Draw state as one DIRECT GIF A+D packet: register 0x49 = 0, six registers by mode, then the viewport's 4 qw
 * (halfwords 2 / 10 + 8 on even fields - the interlace offset; halfwords 18/19 and 26/27 narrowed by scissor_mask). */
void DF_MkDL_View(unsigned int **gpp, union_7a05e625 *vp, int mode)
{
    unsigned int *g = *gpp;
    int n = vp ? 4 : 0;
    unsigned short field = g_odevCur == 0 ? 8 : 0;
    g[0] = (unsigned int)(n + 9) | 0x10000000; g[1] = 0; g[2] = 0; g[3] = 0;
    g[4] = 0; g[5] = 0; g[6] = 0; g[7] = (unsigned int)(n + 8) | 0x51000000;
    g[8] = (unsigned int)(n + 7) | 0x8000; g[9] = 0x10004000; g[10] = 0xe; g[11] = 0;
    put64(g + 12, 0); put64(g + 14, 0x49);
    unsigned int *q = g + 16;
    switch (mode) {
    case 3:
        put64(q + 0, 0x48); put64(q + 2, 0x42); put64(q + 4, 0x48); put64(q + 6, 0x43);
        put64(q + 8, 0x33001); put64(q + 10, 0x47); put64(q + 12, 0x33001); put64(q + 14, 0x48);
        put64(q + 16, 0); put64(q + 18, 0x1a); put64(q + 20, 0x40); put64(q + 22, 0x1b);
        break;
    case 2:
        put64(q + 0, 0x1400000064ULL); put64(q + 2, 0x42); put64(q + 4, 0x44); put64(q + 6, 0x43);
        put64(q + 8, 0x71001); put64(q + 10, 0x47); put64(q + 12, 0x70ff5); put64(q + 14, 0x48);
        put64(q + 16, 1); put64(q + 18, 0x1a); put64(q + 20, 0x40); put64(q + 22, 0x1b);
        break;
    case 1:
        put64(q + 0, 0x54); put64(q + 2, 0x42); put64(q + 4, 0x44); put64(q + 6, 0x43);
        put64(q + 8, 0x31001); put64(q + 10, 0x47); put64(q + 12, 0x10ff1); put64(q + 14, 0x48);
        put64(q + 16, 0); put64(q + 18, 0x1a); put64(q + 20, 0x40); put64(q + 22, 0x1b);
        break;
    default:
        put64(q + 0, 0xa); put64(q + 2, 0x42);
        put64(q + 4, (unsigned long long)((long long)envmapalpha << 32 | (long long)envmapalphaD << 6 |
                                          (long long)envmapalphaC << 4 | (long long)envmapalphaA |
                                          (long long)envmapalphaB << 2));
        put64(q + 6, 0x43);
        put64(q + 8, 0x7000f); put64(q + 10, 0x47); put64(q + 12, 0x50ff5); put64(q + 14, 0x48);
        put64(q + 16, 0); put64(q + 18, 8); put64(q + 20, 0); put64(q + 22, 9);
        break;
    }
    q += 24;
    if (vp) {
        CopyMemory128b(q, vp, 0x40);
        unsigned short *h = (unsigned short *)q;
        h[2] = h[2] + field;
        h[10] = h[10] + field;
        if (scissor_mask) {
            unsigned short uh = (unsigned short)scissor_mask_uh, lh = (unsigned short)scissor_mask_lh;
            h[18] = (h[18] & 0xf800) | (((h[18] & 0x7ff) + uh) & 0x7ff);
            h[19] = (h[19] & 0xf800) | (((h[19] & 0x7ff) - lh) & 0x7ff);
            h[26] = (h[26] & 0xf800) | (((h[26] & 0x7ff) + uh) & 0x7ff);
            h[27] = (h[27] & 0xf800) | (((h[27] & 0x7ff) - lh) & 0x7ff);
        }
        q += 16;
    }
    *gpp = q;
}

void DF_MkDL_Frame(void)
{
    PS2_FRAME(0x20);
    omd_switch = functab[StgConf.nfsw];
    char *buf = (char *)DLBuffer[BuffID & 1];
    CurACTex = -1;
    unsigned int *g = (unsigned int *)buf;
    MapKickP = buf;
    __MakeBgDl(&g);
    __MakeGeneralDl(&g);
    __MakeSfxDl(&g);
    DF_MkDL_View(&g, ViewportTab[1], 0);
    GL_EndList(&g);
    DLBufferLen = (int)(PS2PTR(g) - PS2PTR(MapKickP));
    if (DLBufferLenMax < DLBufferLen) DLBufferLenMax = DLBufferLen;
}

void DF_MkDL_ResetScreen(unsigned int **gpp, int view, int mode)
{
    unsigned int *g = *gpp;
    GL_Sync(&g);
    GL_SetRegister(&g, 0x4e, 0x31000070);
    GL_SetRegisters(&g, (unsigned long long *)(BuffID ? &DBUFF.draw1 : &DBUFF.draw0), 8);
    DF_MkDL_View(&g, ViewportTab[ViewNum] + view, mode);
    *gpp = g;
}

void DF_MkDL_RegistXLU(float depth, void *drawfunc, void *data)
{
    int n = s_xlu_num;
    if (n < 0x800) {
        s_xlu_list[n].depth = depth;
        s_xlu_list[n].drawfunc = (decltype(s_xlu_list[n].drawfunc))drawfunc;
        s_xlu_list[n].data = data;
        s_xlu_num = s_xlu_num + 1;
    }
}

void *DF_MkDL_GetBuffer(void) { return DLBuffer[BuffID & 1]; }

void DF_MkDL_ViewReset(void)
{
    int *cam = &ViewCamID[0];
    cam[0] = 0;
    ViewNum = 1;
    cam[2] = 2;
    cam[3] = 3;
    cam[1] = 1;
}

/* background models (mflag 0x8000) of the map and object lists */
void MkDL_Init(StModel *map, int mnum, StModel *obj, int onum)
{
    int n = 0;
    for (int i = 0; i < mnum; i++) if (map[i].mflag & 0x8000) n++;
    for (int i = 0; i < onum; i++) if (obj[i].mflag & 0x8000) n++;
    s_numBgList = n;
    s_ppmBgList = n > 0 ? (StModel **)Memm_Alloc(1, n << 2, 0x10) : 0;
    n = 0;
    for (int i = 0; i < mnum; i++) if (map[i].mflag & 0x8000) s_ppmBgList[n++] = &map[i];
    for (int i = 0; i < onum; i++) if (obj[i].mflag & 0x8000) s_ppmBgList[n++] = &obj[i];
}

int MkDL_Create(int numTrans, int numOpacity, int bsize)
{
    DLBufferSize = bsize >> 1;
    DLBuffer[0] = (int *)Memm_Alloc(1, DLBufferSize, 0x10);
    DLBuffer[1] = (int *)Memm_Alloc(1, DLBufferSize, 0x10);
    clip_buff[0] = (unsigned int *)Memm_Alloc(1, 0x40000, 0x10);
    clip_buff[1] = (unsigned int *)Memm_Alloc(1, 0x40000, 0x10);
    int n = numTrans + numOpacity;
    s_ppmGeneList = (decltype(s_ppmGeneList))Memm_Alloc(1, n * 4, 0x10);
    s_pvfGeneList = (decltype(s_pvfGeneList))Memm_Alloc(1, n * 0x10, 0x80);
    int ok = s_ppmGeneList != 0 && s_pvfGeneList != 0;
    MapKickP = 0;
    CUR_DRAW_CAM = 0;
    ZeroMemory32b(CAM_POS, 0x10);
    s_numGeneList = 0;
    s_numOptcmfList = 0;
    CurACTex = -1;
    DLBufferLen = 0;
    DLBufferLenMax = 0;
    return ok;
}

/* translucent draw callbacks in sorted order, with the magic VU1 program loaded and WORLD_SCREEN_MATRIX in VU0
 * vf1-vf4 (the callbacks project with it, see gfxvu0_RotTransPers) */
static void mkdl_xlu(unsigned int **gpp, struct_f21c8821 **ptop, int num, int vid)
{
    PS2_FRAME(0x70);
    unsigned long long reg = XLU_REG;
    unsigned int *g = *gpp;
    GL_Sync(&g);
    g = put_call(g, PS2PTR(g_pmpgVu1Magic));
    g = put_matrix_ref(g, 0x188, PS2PTR(mkdl_effect_vu1_const));   /* into both VU1 buffers */
    g = put_matrix_ref(g, 0x348, PS2PTR(mkdl_effect_vu1_const));
    GL_SetRegister(&g, 0x14, reg);
    GL_Sync(&g);
    vu0_load_vf1_4(WORLD_SCREEN_MATRIX);
    for (int i = 0; i < num; i++) {
        struct_f21c8821 *x = ptop[i];
        if (x && x->drawfunc) gcall(x->drawfunc, &g, x->data, vid);
    }
    GL_Sync(&g);
    g = put_call(g, PS2PTR(g_pmpgVu12f));
    *gpp = g;
}

/* comb sort by depth, farthest first */
static void sort_xlu(struct_f21c8821 **ptop, struct_f21c8821 *top, int num)
{
    for (int i = 0; i < num; i++) ptop[i] = &top[i];
    int gap = num, sorted = 0;
    while (gap > 1 || !sorted) {
        gap = gap * 10 / 13;
        if (gap == 0) gap = 1;
        else if (gap == 9 || gap == 10) gap = 11;
        sorted = 1;
        for (int i = 0; i < num - gap; i++) {
            struct_f21c8821 *a = ptop[i], *b = ptop[i + gap];
            if (a->depth < b->depth) {
                ptop[i] = b;
                sorted = 0;
                ptop[i + gap] = a;
            }
        }
    }
}

/* shell sort of the general list by view z, farthest first */
static void ZSortAllModel(void)
{
    int h = 1;
    while (h < s_numGeneList) h = h * 3 + 1;
    for (; h > 0; h /= 3) {
        for (int i = h; i < s_numGeneList; i++) {
            for (int j = i - h; j >= 0; j -= h) {
                float (*a)[4] = &s_pvfGeneList[j], (*b)[4] = &s_pvfGeneList[j + h];
                if (!((*a)[2] < (*b)[2])) break;
                StModel *t = s_ppmGeneList[j];
                s_ppmGeneList[j] = s_ppmGeneList[j + h];
                s_ppmGeneList[j + h] = t;
                float tv[4];
                memcpy(tv, *b, 16);
                memcpy(*b, *a, 16);
                memcpy(*a, tv, 16);
            }
        }
    }
}

/* view frustum culling of a bounding sphere at view position vv: use 0x400 = culled, clip bits 1/2 left/right,
 * 4/8 top/bottom, 0x10 near (sphere crosses those planes) */
static void __Vfc(StModel *mp, float *vv)
{
    if (mp->mflag & 0x20) return;
    float r;
    if (mp->type == 3) {
        r = mp->R;
        if (r <= mp->tall) r = mp->tall;
    } else {
        r = mp->mlist->sphere[3];
    }
    float z = vv[2];
    if (z <= -r) { mp->use |= 0x400; return; }
    if ((mp->mflag & 0x400) && !(z < CopyOfSysInfo.farculldist + r)) { mp->use |= 0x400; return; }
    float e = s_aax * z + r * s_bbx;
    if (!(vv[0] * vv[0] <= e * e)) { mp->use |= 0x400; return; }
    e = s_aay * z + r * s_bby;
    if (!(vv[1] * vv[1] <= e * e)) { mp->use |= 0x400; return; }
    if (z < r) mp->clip |= 0x10;
    z = vv[2];
    if (z < r * s_xR) {
        e = s_aax * z - r * s_bbx;
        if (!(vv[0] <= e)) mp->clip |= 2;
        if (vv[0] < -e) mp->clip |= 1;
        e = s_aay * z - r * s_bby;
        if (!(vv[1] <= e)) mp->clip |= 8;
        if (vv[1] < -e) mp->clip |= 4;
    }
}

/* adds mp with view position v (z - addz) to the general or (use 0x2000) the replaced-opacity list */
static inline void regist(StModel *mp, const float *v, float addz)
{
    int *cnt;
    float (*vf)[4];
    if (mp->use & 0x2000) {
        cnt = &s_numOptcmfList;
        s_ppmOptcmfList[*cnt] = mp;
        vf = &s_pvfOptcmfList[*cnt];
    } else {
        cnt = &s_numGeneList;
        s_ppmGeneList[*cnt] = mp;
        vf = &s_pvfGeneList[*cnt];
    }
    if (vf) {
        memcpy(*vf, v, 16);
        (*vf)[2] = (*vf)[2] - addz;
    }
    *cnt = *cnt + 1;
}

/* map / object / destructible models: sphere test, near/far LOD flag (use 0x1000) */
static void regist_map_models(StModel *mp, int num, const float (*mat)[4], int view)
{
    for (int i = 0; i < num; i++, mp++) {
        mp->use &= 0xfbff;
        mp->clip = 0;
        if ((mp->use & 1) || (mp->mflag & 0x8000) || !(mp->on_layer & (1 << (view & 31)))) continue;
        float v[4] __attribute__((aligned(16)));
        xform1(v, mat, mp->mlist->sphere);
        __Vfc(mp, v);
        unsigned short use = mp->use;
        if (use & 0x400) continue;
        if (Array->head.sysinfo.nearfaron) {
            if (v[2] < mp->mlist->sphere[3] + mp->mlist->far_dist) mp->use = use & 0xefff;
            else mp->use = use | 0x1000;
        }
        regist(mp, v, mp->array->addz);
    }
}

/* characters / AC parts / items: registered at the view position of loc */
static inline void regist_loc(StModel *mp, const float (*mat)[4], float addz)
{
    float v[4] __attribute__((aligned(16)));
    xform1(v, mat, mp->loc);
    regist(mp, v, addz);
}
#define regist_part(mp, mat) regist_loc(mp, mat, 0.0f)

static void DF_OneFrame_DrawModelRegist(float (*mat)[4], int view)
{
    int cnum = StgInfo.cnum, mnum = Array->head.mnum, onum = Array->head.onum, dnum = Array->head.dnum;
    s_numGeneList = 0;
    s_numOptcmfList = 0;
    const float (*m)[4] = (const float (*)[4])mat;
    vu0_load_vf1_4(m);
    if (StgConf.draw_map) regist_map_models(Map, mnum, m, view);
    if (StgConf.draw_obj) {
        regist_map_models(Obj, onum, m, view);
        if (Dobj) regist_map_models(Dobj, dnum, m, view);
    }
    if (StgConf.draw_chr) {
        StModel *mp = Chr;
        for (int i = 0; i < cnum; i++, mp++) {
            mp->use &= 0xfbff;
            mp->clip = 0;
            if ((mp->use & 1) || !(mp->on_layer & (1 << (view & 31)))) continue;
            float v[4] __attribute__((aligned(16)));
            DF_GetModelCenter(v, mp);
            xform1(v, m, v);
            __Vfc(mp, v);
            if (mp->use & 0x400) continue;
            float r = mp->R;
            if (r <= mp->tall) r = mp->tall;
            if (v[2] < 400.0f + r) mp->use &= 0xefff;
            else mp->use |= 0x1000;
            regist_loc(mp, m, mp->array->addz);   /* culled by its centre, sorted by loc */
        }
    }
    if (StgConf.draw_ac) {
        for (int i = 0; i < NowPlayerNum; i++)
            if (!(MyAC[i].use & 1)) regist_part(&MyAC[i], m);
        for (int i = 0; i < NowPlayerNum; i++) {
            StModel *mp = g_pmodelArmWepR[i];
            if (mp && !g_ac[i].param.armsp->type && mp->on && !(mp->use & 1)) regist_part(mp, m);
        }
        for (int i = 0; i < NowPlayerNum; i++) {
            StModel *mp = g_pmodelArmWepL[i];
            if (mp && !g_ac[i].param.armsp->type && mp->on && !(mp->use & 1)) regist_part(mp, m);
        }
        for (int i = 0; i < NowPlayerNum; i++)
            if (MyLBW[i].on && !(MyLBW[i].use & 1)) regist_part(&MyLBW[i], m);
        for (int i = 0; i < NowPlayerNum; i++)
            if (MyRBW[i].on && !(MyRBW[i].use & 1)) regist_part(&MyRBW[i], m);
        for (int i = 0; i < NowPlayerNum; i++) {
            if (MyLEX[i].on && !(MyLEX[i].use & 1)) {
                regist_part(&MyLEX[i], m);
                regist_part(&MyREX[i], m);
            }
        }
        for (int i = 0; i < NowPlayerNum; i++)
            if (MyINS[i].on && !(MyINS[i].use & 1)) regist_part(&MyINS[i], m);
    }
    StModel *it = Itm;
    for (int i = 0; i < 16; i++, it++)
        if (it->on && !(it->use & 1)) regist_part(it, m);
}

static void __MakeSfxDl(unsigned int **gpp)
{
    PS2_FRAME(0x90);
    unsigned int *g = *gpp;
    GL_Sync(&g);
    if (StgConf.txx_sfx) {
        g = put_call(g, PS2PTR(BLTtagSFX));
        GL_SetRegister(&g, 0x3f, 0);
    }
    for (int vpid = 0; vpid < ViewNum; vpid++) {
        CUR_DRAW_CAM = (CameraSt *)DF_VP_GetCamera(vpid);
        int cockpit = CUR_DRAW_CAM ? CUR_DRAW_CAM->view_mode == 1 : 0;
        DF_MkDL_View(&g, ViewportTab[ViewNum] + vpid, 0);
        DF_VP_MakeDL(&g, vpid);
        DF_VP_GetMatrix(vpid, WORLD_SCREEN_MATRIX, 0, 0);
        DF_VP_GetMatrix(vpid, CAMERA_MATRIX1, 1, 0);
        DF_VP_GetMatrix(vpid, CAMERA_MATRIX2, 2, 0);
        StModel *par = MyAC[vpid].nowparent;
        if (par && par->mlist) memcpy(FogParam, par->mlist->lp_fog->fog, 16);
        /* only .w is set in the original (xyz = stale stack); zero here */
        float foga[4] __attribute__((aligned(16))) = {0, 0, 0, FogParam[2]};
        float fogb[4] __attribute__((aligned(16))) = {0, 0, 0, FogParam[3]};
        memcpy(&VU0()->vf[12], foga, 16);
        memcpy(&VU0()->vf[13], fogb, 16);
        DF_MAGIC2_SetFog(&g, foga, fogb);
        if (vpid == 0) Magic2MatrixSW = Magic2MatrixSW == 0;
        Magic2MatrixSC = (unsigned char)vpid;
        s_xlu_num = 0;
        if (StgConf.draw_mag2) DF_Magic2MatrixUpdate(CAMERA_MATRIX1, cockpit);
        CLOUD_Regist(CAMERA_MATRIX1);
        sort_xlu(s_xlu_plist, s_xlu_list, s_xlu_num);
        mkdl_xlu(&g, s_xlu_plist, s_xlu_num, vpid);
        if (StgConf.draw_mag3) DF_MkDL_Magic3(&g);
        if (StgConf.draw_eye) DF_EYE2_MkDL(&g, vpid);
    }
    *gpp = g;
}

static void __SubMakeReplacedOpcDl(unsigned int **gpp, int view)
{
    PS2_FRAME(0x70);
    unsigned int *g = *gpp;
    if (!s_numOptcmfList) return;
    g = put_call(g, PS2PTR(g_pmpgVu1ReplacedOpc));
    GL_Sync(&g);
    GL_SetRegister(&g, 0x4c, 0x20401da);
    GL_SetRegister(&g, 0x40, 0xff000000ff0000ULL);
    GL_SetRegister(&g, 0x18, 0x780000007800ULL);
    GL_SetRegister(&g, 0x4e, 0x131000070ULL);
    GL_SetRegister(&g, 0x3f, 0);
    GL_SetRegister(&g, 0x47, 0x30000);
    GL_SetRegister(&g, 6, 0xa24120700ULL);
    GL_SetRegister(&g, 0x14, 0x60);
    GL_RectUV(&g, __SubMakeReplacedOpcDl__rect, __SubMakeReplacedOpcDl__uv, 0);
    GL_SetRegister(&g, 0x4c, 0x80038);
    GL_SetRegister(&g, 0x40, 0xdf000001ff0000ULL);
    GL_SetRegister(&g, 0x18, 0x790000007000ULL);
    GL_SetRegister(&g, 0x4e, 0x31000070);
    GL_SetRegister(&g, 0x47, 0x51003);
    StOpcInfo *oi = s_ppmOptcmfList[0]->pinfoOpc;
    __SubMakeReplacedOpcDl__vAlpba[3] = oi ? oi->alpha : 1.0f;
    GL_LoadVector(&g, 0x17d, __SubMakeReplacedOpcDl__vAlpba);
    GL_LoadVector(&g, 0x33d, __SubMakeReplacedOpcDl__vAlpba);
    GL_LoadVector(&g, 0x17e, __SubMakeReplacedOpcDl__tex0_gif);
    GL_LoadVector(&g, 0x33e, __SubMakeReplacedOpcDl__tex0_gif);
    GL_LoadVector(&g, 0x17f, __SubMakeReplacedOpcDl__tex0);
    GL_LoadVector(&g, 0x33f, __SubMakeReplacedOpcDl__tex0);
    GL_SetRegister(&g, 9, 5);
    int n = s_numOptcmfList;
    unsigned int *g2 = g;
    for (int i = 0; i < n; i++) {
        StModel *mp = s_ppmOptcmfList[i];
        switch (mp->type) {
        case 1: case 2: case 3: case 4: case 6: case 7: case 8:
            __DrawModelReplacedOpc(&g2, mp, view);
            break;
        }
    }
    g = g2;
    GL_SetRegister(&g, 9, 0);
    *gpp = g;
}

static void set_frustum(float fx)
{
    float scrz = CUR_DRAW_CAM->scrz;
    if (hook_cull_frustum) hook_cull_frustum(&fx, &scrz);   /* step 5.2 Hor+ culling margin (NULL = original) */
    float ax = ps2_div(fx, scrz), ay = ps2_div(224.0f, scrz);
    s_bbx = ps2_sqrt(1.0f + ax * ax);
    float by = ps2_sqrt(1.0f + ay * ay);
    s_aax = ax;
    s_aay = ay;
    s_bby = by;
    s_fovx = ps2_atanf(ax);
    s_cos_fovx = fsmath_CosVU0(s_fovx);
    s_sin_fovx = fsmath_SinVU0(s_fovx);
    s_fovy = ps2_atanf(ay);
    s_cos_fovy = fsmath_CosVU0(s_fovy);
    s_sin_fovy = fsmath_SinVU0(s_fovy);
}

static void __MakeGeneralDl(unsigned int **gpp)
{
    PS2_FRAME(0x70);
    unsigned int *g = *gpp;
    if (!StgConf.envball) g = put_call(g, PS2PTR(g_pmpgVu1AmbPliFogRef));
    if (StgConf.txx_map) {
        GL_Sync(&g);
        g = put_call(g, PS2PTR(BLTtagMap));
    }
    if (StgConf.multx) MultiTex_Draw(&g);
    for (int vpid = 0; vpid < ViewNum; vpid++) {
        CUR_DRAW_CAM = (CameraSt *)DF_VP_GetCamera(vpid);
        if (!CUR_DRAW_CAM) continue;
        set_frustum(320.0f);   /* = 256 / 0.8 */
        GL_Sync(&g);
        if (StgConf.envball) DF_MkDL_EnvBall(&g, vpid, 0x3c00, 0x3bc0, &Envball);
        GL_LoadVector(&g, 0x4a, CAM_POS[vpid]);
        DF_MkDL_View(&g, ViewportTab[ViewNum] + vpid, 0);
        DF_VP_MakeDL(&g, vpid);
        DF_VP_GetMatrix(vpid, CAMERA_MATRIX1, 1, 0);
        DF_VP_GetMatrix(vpid, CAMERA_MATRIX2, 2, 0);
        DF_OneFrame_DrawModelRegist(CAMERA_MATRIX1, vpid);
        ZSortAllModel();
        __DrawModelArray(&g, s_ppmGeneList, s_numGeneList, vpid);
        MkDL_ACShadows(&g, vpid);
        __SubMakeReplacedOpcDl(&g, vpid);
    }
    *gpp = g;
}

static void __MakeBgDl(unsigned int **gpp)
{
    PS2_FRAME(0x70);
    if (s_numBgList <= 0) return;
    unsigned int *g = put_call(*gpp, PS2PTR(g_pmpgVu1OnlyAmg));
    unsigned int *bg = BLTtagBG;
    if (bg && StgConf.txx_bg) g = put_call(g, PS2PTR(bg));
    for (int vpid = 0; vpid < ViewNum; vpid++) {
        CUR_DRAW_CAM = (CameraSt *)DF_VP_GetCamera(vpid);
        if (!CUR_DRAW_CAM) continue;
        set_frustum(320.0f);
        GL_Sync(&g);
        GL_LoadVector(&g, 0x4a, CAM_POS[vpid]);
        DF_MkDL_View(&g, ViewportTab[ViewNum] + vpid, 0);
        DF_VP_MakeDL(&g, vpid);
        DF_VP_GetMatrix(vpid, CAMERA_MATRIX1, 1, 0);
        DF_VP_GetMatrix(vpid, CAMERA_MATRIX2, 2, 0);
        __DrawModelArray(&g, s_ppmBgList, s_numBgList, vpid);
    }
    *gpp = g;
}

#ifdef ACNB_SOURCEPORT
/* the original's initialized tables (generated by scripts/sp_datagen.py from the PS2 image) */
struct OmdTableSt *(*functab[2])(struct StModel *) = {   /* main 0x27ad48 */
    omd_switch_dist, omd_switch_plnum
};
#endif
