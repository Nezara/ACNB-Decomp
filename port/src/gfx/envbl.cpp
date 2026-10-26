/* src/gfx/envbl.cpp — the environment ball: a small sphere model (Envball) rendered every frame into a 64x64 render
 * target (fbp / zbp) that the stage models sample as their reflection texture. OutputOMD_EnvBall turns the model's
 * polygon lists into one prebuilt VIF1 packet at mp->vu1mp (load time); DF_MkDL_EnvBall, per viewport, rebuilds the
 * view-screen matrix from camera 0's angles (envmatrix2, double-buffered by BuffID because the DL REFs it), clears the
 * Z and colour pages with GL_SetDrawEnv1 and CALLs the VU1 program g_pmpgVu1EnvBall over that packet.
 * DF_EnvBallInit builds an alternative kick list (EnvBallKickP) that nothing in the main ELF reads. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/local/src/gfx/envbl.h"

/* DMA cnt 1 + UNPACK V4-32 x4 to VU vuaddr (word 3), then DMA ref 4 qw -> addr (sdr / sdl pair at the quad start) */
static inline unsigned int *envbl_matrix_ref(unsigned int *p, unsigned int vuaddr, unsigned int addr)
{
    p[0] = 0x10000001; p[1] = 0; p[2] = 0; p[3] = 0;
    p[4] = 0; p[5] = 0; p[6] = 0; p[7] = 0x6c040000 | vuaddr;
    p[8] = 0x30000004; p[9] = addr; p[10] = 0; p[11] = 0;
    return p + 12;
}

/* DMA call tag to addr */
static inline unsigned int *envbl_call(unsigned int *p, unsigned int addr)
{
    p[0] = 0x50000000; p[1] = addr; p[2] = 0; p[3] = 0;
    return p + 4;
}

/* The VIF1 packet of the env ball model: per polygon list a RET/CNT block {DMA tag, UNPACK + GIF A+D (TEX1 / TEX0
 * twice), UNPACK of the vertices, two GIF tags (the second with ABE / FGE set), vertex records, MSCAL / MSCNT}.
 * Consecutive lists with the same TEX0 / TEX1 are merged into the previous block while it holds < 0x3a vertices (the
 * first two vertices of a merged list get the ADC bit). Returns the end of the packet. */
char *OutputOMD_EnvBall(StModel *mp, char *wp)
{
    PS2_FRAME(0x240);
    /* the original's stack buffers (sp+0x150..0x23f): every quad is built there and memcpy'd into the packet. The
     * vertex record's second quad (0x210) is never written: every vertex copies stale stack (VU1 ignores it). */
    struct Buf {
        unsigned int vif[4];        /* 0x150 VIF quad {0, 0, 0, code} */
        unsigned int dma[4];        /* 0x160 DMA tag */
        unsigned int gif1t[4];      /* 0x170 GIF tag template NLOOP 1 EOP A+D */
        unsigned int gif2t[4];      /* 0x180 GIF tag template NLOOP 2 A+D */
        unsigned int gif[4];        /* 0x190 vertex GIF tag */
        unsigned long long ad[4];   /* 0x1a0 {tex1, TEX1_1, tex, TEX0_1}; 0x1b0 = {tex, TEX0_1} */
        unsigned char dead[0x20];   /* 0x1c0 memset per object, never read */
        unsigned long long prev[4]; /* 0x1e0 previous tex1, 0x1f0 previous tex */
        unsigned int vtx[16];       /* 0x200 vertex record: xyz1, (stale), uv 1 adc */
    };
    static_assert(sizeof(Buf) == 0xf0);
    PS2_STACK(Buf, b, 0x150);

    b.gif2t[0] = 2; b.gif2t[1] = 0x10004000; b.gif2t[2] = 0xe; b.gif2t[3] = 0;
    b.gif1t[0] = 0x8001; b.gif1t[1] = 0x10004000; b.gif1t[2] = 0xe; b.gif1t[3] = 0;
    unsigned int *tagp = nullptr, *unpackp = nullptr, *tag1p = nullptr, *tag2p = nullptr;   /* sp+0x10c f0 e0 d0 */
    unsigned int code = 0;                                                                   /* sp+0xc0 */
    char *s5 = wp;
    mp->vu1mp = (unsigned int *)wp;
    _objtbl *ot = mp->otp;
    for (unsigned int i = 0; i < mp->nobj; i++, ot++) {
        memset(b.prev, 0, 0x20);
        memset(b.dead, 0, 0x20);
        unsigned int nvtx = 0;   /* sp+0xb0: vertices in the current (merged) block */
        int first = 1;           /* sp+0x110: next new block starts the VU1 program (MSCAL) */
        char *ph = (char *)ot->php;
        for (unsigned int j = 0; j < ot->p_num; j++) {
            struct_de4d68f5 *h = (struct_de4d68f5 *)ph;
            unsigned long long tex, tex1;
            memcpy(&tex, ph + 8, 8);
            memcpy(&tex1, ph + 0x18, 8);
            unsigned int n = h->pv_num;
            int merge = 0;
            if (j != 0 && b.prev[2] == tex && b.prev[0] == tex1) merge = 1;
            /* last list of the object (p_num re-read through mp->otp + i) */
            int last = j == (unsigned int)(mp->otp[i].p_num - 1);
            if (merge && (int)(nvtx + n) < 0x3a) {
                *tagp = ((*tagp & 0xffff) + n * 4) | (last ? 0x60000000u : 0x10000000u);
                s5 -= 0x10;   /* overwrite the previous block's end quad */
                *unpackp = ((((*unpackp & 0xff0000) >> 16) + n * 4) << 16) | 0x6c00c000;
                unsigned int v = ((*tag1p & 0x7fff) + n) | 0x8000;
                *tag1p = v;
                *tag2p = v;
                nvtx += n;
            } else {
                merge = 0;
                nvtx = n;
                b.dma[0] = ((n & 0xffff) * 4 + 0xa) | (last ? 0x60000000u : 0x10000000u);
                b.dma[1] = 0; b.dma[2] = 0; b.dma[3] = 0;
                tagp = (unsigned int *)s5;
                memcpy(s5, b.dma, 0x10);
                b.vif[0] = 0; b.vif[1] = 0; b.vif[2] = 0; b.vif[3] = 0x6c05c100;   /* UNPACK 5 qw to 0x100 */
                memcpy(s5 + 0x10, b.vif, 0x10);
                memcpy(s5 + 0x20, b.gif2t, 0x10);
                b.ad[2] = tex; b.prev[2] = tex; b.ad[3] = 6;
                b.ad[0] = tex1; b.prev[0] = tex1; b.ad[1] = 0x14;
                memcpy(s5 + 0x30, b.ad, 0x20);
                memcpy(s5 + 0x50, b.gif1t, 0x10);
                memcpy(s5 + 0x60, &b.ad[2], 0x10);
                b.vif[0] = 0; b.vif[1] = 0; b.vif[2] = 0;
                unpackp = (unsigned int *)(s5 + 0x7c);
                b.vif[3] = ((n * 4 + 2) << 16) | 0x6c00c000;
                memcpy(s5 + 0x70, b.vif, 0x10);
                unsigned int prim = (h->flag & 0x7ffu) << 15;
                tag1p = (unsigned int *)(s5 + 0x80);
                b.gif[0] = n | 0x8000;
                b.gif[1] = 0x30004000u | (prim & 0xffdfffffu);   /* ABE cleared */
                b.gif[2] = 0x412; b.gif[3] = 0;
                memcpy(s5 + 0x80, b.gif, 0x10);
                tag2p = (unsigned int *)(s5 + 0x90);
                b.gif[0] = n | 0x8000;
                b.gif[1] = 0x30004000u | prim | 0x01300000u;
                b.gif[2] = 0x412; b.gif[3] = 0;
                memcpy(s5 + 0x90, b.gif, 0x10);
                s5 += 0xa0;
            }
            char *pv = ph + 0x20;
            for (unsigned int k = 0; k < h->pv_num; k++, pv += 0x20) {
                unsigned short idx;
                memcpy(&idx, pv, 2);
                memcpy(&b.vtx[0], ot->vp[idx], 12);
                b.vtx[3] = 0x3f800000;
                memcpy(&b.vtx[8], pv + 8, 8);
                b.vtx[10] = 0x3f800000;
                b.vtx[11] = (merge && k < 2) ? 0x8000 : 0;
                memcpy(s5, b.vtx, 0x40);
                s5 += 0x40;
            }
            b.vif[0] = 0; b.vif[1] = 0; b.vif[2] = 0;
            if (!merge) {
                if (first) { first = 0; code = 0x14000200; }   /* MSCAL 0x200 */
                else code = 0x17000000;                        /* MSCNT */
            }
            b.vif[3] = code;
            memcpy(s5, b.vif, 0x10);
            ph = pv;
            s5 += 0x10;
        }
    }
    return s5;
}

/* view-screen matrix of the 64x64 target (scrz 640, centre 2048, z 0..16777215, near 1, far 65536) */
static void envbl_view_screen(float (*m)[4])
{
    sceVu0ViewScreenMatrix(m, 640.0f, 1.0f, 1.0f, 2048.0f, 2048.0f, 0.0f, ps2_u2f(0x4b7fffff), 1.0f, 65536.0f);
}

/* Per viewport: env-ball matrix = view-screen * T(0, 0, 140) * RX(-cam x) * RY(-cam y) of camera 0 (ViewCamID[0] for
 * every view - split screen shares player 1's angles), clear the Z page then the colour page, ZBUF / TEST / CLAMP,
 * the two matrix REFs and the CALLs of the VU1 program, the model packet and the 2f program, then restore ZBUF and
 * the frame's draw environment. */
void DF_MkDL_EnvBall(unsigned int **gpp, int view, int fbp, int zbp, StModel *mp)
{
    PS2_FRAME(0x170);   /* sceVu0ViewScreenMatrix (interpreted) takes farz at sp+0 */
    PS2_STACK(float[4][4], RY, 0xa0);
    PS2_STACK(float[4][4], RX, 0xe0);
    PS2_STACK(float[4][4], T, 0x120);
    alignas(16) static const float trans[4] = {0.0f, 0.0f, 140.0f, 1.0f};

    float (*m)[4] = envmatrix2[BuffID][view];
    unsigned int *g = *gpp;
    envbl_view_screen(m);
    /* oracle: sceVu0ViewScreenMatrix (interpreted, frame 0xa0) saved its ra at sp-0x50 - the bridge's return sentinel
     * here, DF_MkDL_EnvBall+0xa0 in the original. GL_SetDrawEnv1's env PRMODECONT slot is that word: sceGsSetDefDrawEnv
     * only sets bit 0, so the stale ra goes into the DL (GS-invisible upper bits) */
    PS2_STACK(unsigned long long, vsm_ra, -0x50);
    vsm_ra = (unsigned long long)PS2PTR(GFN(DF_MkDL_EnvBall)) + 0xa0;
    (void)vsm_ra;
    CameraSt *cam = &CAM[ViewCamID[0]];
    sceVu0RotMatrixX(RX, IdentityMtx, -cam->ang[0]);
    sceVu0RotMatrixY(RY, IdentityMtx, -cam->ang[1]);
    sceVu0TransMatrix(T, IdentityMtx, (float *)trans);
    vu_mul_matrix(T, T, RX);
    vu_mul_matrix(T, T, RY);
    float old[4][4];
    memcpy(old, m, sizeof old);
    vu_mul_matrix(m, m, T);
    /* VU0 as the inline asm leaves it: vf1-4 = old m, vf22-25 = new m, ACC = old m * T[3] without the w term */
    float acc[4];
    for (int i = 0; i < 4; i++) {
        PS2_VF_MIRROR(1 + i, old[i]);
        PS2_VF_MIRROR(22 + i, m[i]);
        float a = old[0][i] * T[3][0];
        a = a + old[1][i] * T[3][1];
        acc[i] = a + old[2][i] * T[3][2];
    }
    PS2_ACC_MIRROR(acc);
    (void)acc;

    ps2_rq_tag_packet(g, RQ_K_ENV, RQ_OBJID(RQ_OC_TEXTURE, fbp), view);   /* render tag (host only) */
    GL_SetDrawEnv1(&g, 64, 64, 0x31, zbp, clc);   /* PSMZ24 */
    GL_SetDrawEnv1(&g, 64, 64, 0, fbp, clc);
    GL_SetRegister(&g, 0x4e, (unsigned long long)(long long)(zbp / 32) | 0x31000000u);   /* ZBUF_1 */
    GL_SetRegister(&g, 0x47, 0x51000);   /* TEST_1 */
    GL_SetRegister(&g, 8, 0);            /* CLAMP_1 */
    g = envbl_matrix_ref(g, 0, PS2PTR(envmatrix2[BuffID][view]));
    g = envbl_matrix_ref(g, 4, PS2PTR(mp->matrix2));
    g = envbl_call(g, PS2PTR(g_pmpgVu1EnvBall));
    g = envbl_call(g, PS2PTR(mp->vu1mp));
    g = envbl_call(g, PS2PTR(g_pmpgVu12f));
    ps2_rq_tag_packet(g, RQ_K_NONE, 0, 0);   /* the ZBUF / DBUFF restore belongs to the frame, not the pass */
    GL_SetRegister(&g, 0x4e, 0x31000070);
    GL_SetRegisters(&g, (unsigned long long *)(BuffID ? &DBUFF.draw1 : &DBUFF.draw0), 8);
    *gpp = g;
}

/* Load time: envmatrix, mp->matrix2 = identity and the kick list EnvBallKickP {envmatrix REF, matrix2 REF, CALL of the
 * model packet, END} (written, never read in the main ELF; it REFs envmatrix, not envmatrix2) */
void DF_EnvBallInit(StModel *mp)
{
    PS2_FRAME(0x40);   /* sceVu0ViewScreenMatrix (interpreted) takes farz at sp+0 */
    envbl_view_screen(envmatrix);
    sceVu0CopyMatrix(mp->matrix2, IdentityMtx);
    unsigned int *p = (unsigned int *)Memm_Alloc(1, 0x80, 0x10);
    EnvBallKickP = (char *)p;
    p = envbl_matrix_ref(p, 0, PS2PTR(envmatrix));
    p = envbl_matrix_ref(p, 4, PS2PTR(mp->matrix2));
    p = envbl_call(p, PS2PTR(mp->vu1mp));
    GL_EndList(&p);
}
