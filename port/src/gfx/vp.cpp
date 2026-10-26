/* src/gfx/vp.cpp - viewports (whole file): VPNum entries at VPTop, each with a camera pointer, double-buffered
 * (flag 0/1) world-screen / world-view / third matrices and two pre-built DMA chains (dl[flag]) that upload wsmat and
 * wvmat of that buffer to VU1 (UNPACK V4-32 at VU addresses 0 and 0x1c). CameraProc swaps the buffers and stores the
 * frame's matrices; the mkdl display-list builders fetch them and emit a DMA call to the current chain. */
#include "acnb/port.h"
#include "acnb/sdk.h"

/* DMA call (qwc 0) to the viewport's current matrix chain */
void DF_VP_MakeDL(unsigned int **gpp, int vpid)
{
    if ((unsigned int)vpid < VPNum) {
        struct_af216808 *vp = &VPTop[vpid];
        unsigned int *p = *gpp;
        p[0] = 0x50000000;
        p[1] = PS2PTR(vp->dl[vp->flag]);   /* the raw flag byte indexes dl[] */
        p[2] = 0;
        p[3] = 0;
        *gpp = p + 4;
    }
}

/* flag 0 reads buffer (vp->flag != 0), otherwise (vp->flag == 0); option 0 ws, 1 wv, 2 the third matrix */
void DF_VP_GetMatrix(unsigned int vpid, float (*mat)[4], int option, int flag)
{
    if (vpid < VPNum) {
        struct_af216808 *vp = &VPTop[vpid];
        int i = flag ? vp->flag == 0 : vp->flag != 0;
        if (option == 2) sceVu0CopyMatrix(mat, vp->wvma2[i]);
        else if (option == 1) sceVu0CopyMatrix(mat, vp->wvmat[i]);
        else if (option == 0) sceVu0CopyMatrix(mat, vp->wsmat[i]);
    }
}

/* writes buffer vp->flag (raw byte, re-read for each copy) */
void DF_VP_SetMatrix(unsigned int vpid, float (*ws)[4], float (*wv)[4], float (*iwv)[4])
{
    if (vpid < VPNum) {
        struct_af216808 *vp = &VPTop[vpid];
        sceVu0CopyMatrix(vp->wsmat[vp->flag], ws);
        sceVu0CopyMatrix(vp->wvmat[vp->flag], wv);
        sceVu0CopyMatrix(vp->wvma2[vp->flag], iwv);
    }
}

void *DF_VP_GetCamera(int vpid)
{
    return (unsigned int)vpid < VPNum ? VPTop[vpid].camp : 0;
}

void DF_VP_SetCamera(int vpid, void *camp)
{
    if ((unsigned int)vpid < VPNum) VPTop[vpid].camp = camp;
}

void DF_VP_Swap(void)
{
    struct_af216808 *vp = VPTop;
    for (unsigned int i = 0; i < VPNum; i++, vp++) vp->flag ^= 1;
}

/* DMA cnt (qwc 1: VIF NOP x3 + UNPACK V4-32 num 4 at VU address addr) + DMA ref (qwc 4) to the matrix */
static unsigned int *vp_upload(unsigned int *p, unsigned int addr, float (*m)[4])
{
    p[0] = 0x10000001;
    p[1] = 0; p[2] = 0; p[3] = 0; p[4] = 0; p[5] = 0; p[6] = 0;
    p[7] = 0x6c040000 | addr;
    p[8] = 0x30000004;
    p[9] = PS2PTR(m);
    p[10] = 0;
    p[11] = 0;
    return p + 12;
}

/* (ZeroMemory32b counts 32-bit words but gets the byte size: it clears 4x the allocation - kept) */
void *DF_VP_New(unsigned int num)
{
    PS2_FRAME(0x40);
    unsigned int size = num * 0x1a0;
    VPTop = (struct_af216808 *)Memm_Alloc(1, size, 0x10);
    VPNum = num;
    ZeroMemory32b(VPTop, size);
    struct_af216808 *vp = VPTop;
    for (unsigned int i = 0; i < VPNum; i++, vp++) {
        for (int b = 0; b < 2; b++) {
            unsigned int *p = (unsigned int *)Memm_Alloc(1, 0x70, 0x10);
            vp->dl[b] = p;
            p = vp_upload(p, 0, vp->wsmat[b]);
            p = vp_upload(p, 0x1c, vp->wvmat[b]);
            GL_EndList(&p);
        }
    }
    return VPTop;
}
