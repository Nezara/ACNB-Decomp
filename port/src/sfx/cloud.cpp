/* src/sfx/cloud.cpp (OLSORTIE) - sky clouds: a pool of 0x50-byte cloud records (s_CloudMng.bufp .. curp, 0x14000-byte
 * buffer) emitted by the stage's 32 cloud definitions (deftab). Native: CLOUD_Init (buffers, once per stage),
 * CLOUD_SetDefAll (the stage's definitions), CLOUD_Proc (steps every cloud) and CLOUD_Regist (view-space position,
 * visible clouds into the translucent draw list). The cloud programs (__cloud_ExecCloud / MakeCloud / NewCloud /
 * MkdlCloud), CLOUD_New / Stop / Reset stay interpreted; the replay stages have no clouds (both loops run 0 times). */
#include "acnb/vu0m.h"
#include <string.h>

/* vloc = wv * (loc.xyz, 1) (VU0 acc chain; vf1-vf4 = wv, vf16 = the result stay live); a cloud in front of the
 * camera (not vloc.z < 0, NaN included) is registered with its depth */
void CLOUD_Regist(float (*wv)[4])
{
    PS2_FRAME(0x30);
    for (int i = 0; i < 4; i++) PS2_VF_MIRROR(1 + i, wv[i]);
    for (struct_9bf5f6a0 *p = s_CloudMng.bufp; p < s_CloudMng.curp; p++) {
        float v[4] = {p->loc[0], p->loc[1], p->loc[2], 1.0f};
        vu_apply(p->vloc, (const float (*)[4])wv, v);
        PS2_VF_MIRROR(16, p->vloc);
        if (!(p->vloc[2] < 0.0f)) DF_MkDL_RegistXLU(p->vloc[2], (void *)GFN(__cloud_MkdlCloud), p);
    }
}

/* steps every cloud; stop = the stop bit of its definition */
void CLOUD_Proc(void)
{
    PS2_FRAME(0x30);
    struct_9bf5f6a0 *p = s_CloudMng.bufp;
    if (p == s_CloudMng.curp) return;
    do {
        int idx = (int)((char *)p->defp - (char *)&s_CloudMng.deftab[0]) / 0x80;
        __cloud_ExecCloud(p, (s_CloudMng.stopflag >> (idx & 31)) & 1);
        p++;
    } while (p != s_CloudMng.curp);
}

void CLOUD_SetDefAll(struct_87c875f8 *pCloudDef)
{
    memcpy(s_CloudMng.deftab, pCloudDef, 0x1000);
}

void CLOUD_Init(void)
{
    PS2_FRAME(0x10);
    memset(&s_CloudMng, 0, 0x1020);
    s_pSortBuf = (struct_9bf5f6a0 **)Memm_Alloc(1, 0x1000, 4);
    s_pCloudBuf = (struct_9bf5f6a0 *)Memm_Alloc(1, 0x14000, 0x10);
    memset(s_pCloudBuf, 0, 0x14000);
    memset(s_pSortBuf, 0, 0x1000);
    s_CloudMng.bufp = s_pCloudBuf;
    s_CloudMng.curp = s_pCloudBuf;
    s_CloudMng.endp = (struct_9bf5f6a0 *)((char *)s_pCloudBuf + 0x14000);
    s_CloudMng.srtp = s_pSortBuf;
}
