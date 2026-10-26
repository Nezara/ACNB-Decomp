/* src/menu/menvbl.cpp - MENVBL, the menu's environment ball (OLMENU): the Envball StModel (an OMD blob copied out of
 * menu data 0 into Envballbuf and relocated in place), its VIF1 model packet (gfx/envbl.c OutputOMD_EnvBall) and,
 * every menu frame, the env-ball world-screen matrix plus a small kick list {REF view-screen matrix, REF matrix2, CALL
 * of the model packet, END} at EnvBallMng.dmap2. DF_MENVBL_MakeVu1Packet (phase 2 group 5a) renders the ball into the
 * env texture (gfx/envbl.c DF_MkDL_EnvBall into frame 0x3c00 / Z 0x3bc0) inside the menu VIF1 chain, then appends the
 * menu view packet. Whole file native.
 * Original quirk kept: DF_MENVBL_Play's first REF tag points at its own (dead after return) stack frame. */
#include <string.h>
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

/* Relocate the TP2 / OMD blob at bufp (header 0x10 bytes, word +8 = object count, then the _objtbl array, then per
 * object its polygon lists, vertices and normals) into mp; anim == 0 also allocates the ObjectSt array (two 4-byte
 * matrix-pointer cells each) and copies each object's position / angle. Returns the end of the blob. */
static void *__MENVBL_ReadTP2Pointer(char *bufp, struct StModel *mp, int anim)
{
    PS2_FRAME(0x60);
    char *rp = bufp + 0x10;
    mp->nobj = (unsigned short)*(unsigned int *)(bufp + 8);
    mp->nmobj = mp->nobj;
    if (!anim) {
        mp->op = (struct ObjectSt *)DF_MMM_Alloc(0, mp->nobj << 7);
        struct ObjectSt *o = mp->op;
        for (int n = mp->nobj; n != 0; n--, o++) {
            o->wrmatp = (float (*)[4][4])DF_MMM_Alloc(1, 4);
            o->rdmatp = (float (*)[4][4])DF_MMM_Alloc(1, 4);
        }
    }
    mp->otp = (struct _objtbl *)rp;
    rp += mp->nobj * 0x50;
    for (int t = 0; t < mp->nmobj; t++) {
        struct _objtbl *obj = &mp->otp[t];
        if (!anim) memcpy(&mp->op[t], obj, 0x20);   /* two lq / sq: position, angle */
        obj->php = (struct struct_de4d68f5 *)rp;
        int n = obj->p_num;
        for (int j = 0; j < n; j++) rp += 0x20 + (*(unsigned short *)rp << 5);   /* header + pv_num vertex records */
        obj->vp = (float (*)[4])rp;
        rp += obj->v_num << 4;
        obj->np = (float (*)[4])rp;
        rp += obj->n_num << 4;
    }
    return rp;
}

void DF_MENVBL_Play(void)
{
    PS2_FRAME(0xf0);   /* sceVu0ViewScreenMatrix takes farz at sp+0; the REF tag below points into this frame */
    PS2_STACK(float[4], zero, 0x50);
    PS2_STACK(float[4][4], wv_mat, 0x60);
#ifdef ACNB_SOURCEPORT
    alignas(16) static float vs_mat[4][4];   /* the original DMAs it from its dead stack frame (sp+0xa0) */
#else
    PS2_STACK(float[4][4], vs_mat, 0xa0);   /* DMA'd from the PS2 stack at kick time (sic) */
#endif
    PS2_STACK(unsigned int *, dmap, 0xec);
    zero[0] = 0.0f; zero[1] = 0.0f; zero[2] = 140.0f; zero[3] = 1.0f;   /* overlay quad 0x1edde40 */
    sceVu0ViewScreenMatrix(vs_mat, 512.0f, 1.0f, 1.0f, 2048.0f, 2048.0f, 0.0f, 65535.0f, 10.0f, 65536.0f);
    sceVu0RotMatrixY(wv_mat, IdentityMtx, -MViewp->cam.ang[1]);
    sceVu0RotMatrixX(wv_mat, wv_mat, -MViewp->cam.ang[0]);
    sceVu0TransMatrix(wv_mat, wv_mat, zero);
    vu_mul_matrix(EnvBallMng.world_screen_mat, vs_mat, wv_mat);   /* inline vmulax / vmadday / vmaddaz / vmaddw */
    sceVu0CopyMatrix(Envball.matrix2, IdentityMtx);

    unsigned int *g = EnvBallMng.dmap2;
    dmap = g;
    EnvBallKickP = (char *)g;
    /* CNT {UNPACK V4-32 x4 -> VU1 0}, REF 4 qw of vs_mat */
    g[0] = 0x10000001; g[1] = 0; g[2] = 0; g[3] = 0;
    g[4] = 0; g[5] = 0; g[6] = 0; g[7] = 0x6c040000;
    g[8] = 0x30000004; g[9] = PS2PTR(vs_mat); g[10] = 0; g[11] = 0;
    g += 12;
    /* CNT {UNPACK V4-32 x4 -> VU1 4}, REF 4 qw of Envball.matrix2 */
    g[0] = 0x10000001; g[1] = 0; g[2] = 0; g[3] = 0;
    g[4] = 0; g[5] = 0; g[6] = 0; g[7] = 0x6c040004;
    g[8] = 0x30000004; g[9] = PS2PTR(Envball.matrix2); g[10] = 0; g[11] = 0;
    g += 12;
    /* CALL the model packet */
    g[0] = 0x50000000; g[1] = PS2PTR(Envball.vu1mp); g[2] = 0; g[3] = 0;
    dmap = g + 4;
    GL_EndList(&dmap);
}

unsigned char *DF_MENVBL_Init(unsigned char *ioBuff)
{
    PS2_FRAME(0x20);
    DF_MFUNC_ClearMemory(&EnvBallMng, 0x14);   /* 0x14 words = the whole 0x50-byte struct */
    unsigned char *p = (unsigned char *)DF_MFUNC_Aligned(ioBuff, 0x10);
    EnvBallMng.dmap = (unsigned int *)p;
    DF_MFUNC_CopyMemory(Envballbuf, DF_MDATA_GetData(MenuMng.dataTbl[0], 1), 0x1000);
    __MENVBL_ReadTP2Pointer(Envballbuf, &Envball, 0);
    char *e = OutputOMD_EnvBall(&Envball, (char *)EnvBallMng.dmap);
    p += e - (char *)EnvBallMng.dmap;
    EnvBallMng.dmap2 = (unsigned int *)p;
    DF_MDRAW_Tm2LoadImageNX(DF_MDATA_GetData(MenuMng.tm2Tbl[2], 0), 0, 0);   /* MenuMng+0x2c */
    return p + 0x80;
}

void DF_MENVBL_MakeVu1Packet(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(unsigned int *, dmap, 0x1c);   /* EnvBall (interpreted) advances it through the pointer */
    dmap = MPktCurrentp;
    DF_MkDL_EnvBall(&dmap, 0, 0x3c00, 0x3bc0, &Envball);
    MPktCurrentp = dmap;
    DF_MVIEW_MakeVU1Packet();
}
