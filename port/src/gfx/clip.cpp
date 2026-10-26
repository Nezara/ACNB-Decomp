/* src/gfx/clip.cpp — clips model triangle strips against a plane (DF_MkDL_ClipModel, used for models crossing the
 * near plane) and rebuilds them as VU1 batches: each batch = DMA cnt tag + 6 qw texture/A+D data + UNPACK of
 * 2 GIF tags and vtx_num vertices (4 qw each: pos, normal, st, rgba), closed by an MSCAL 0x200 / MSCNT qword.
 * Vertices on the cut are interpolated (__nakas_mkdl_cross_point_Mk3); the last one is kept in cross_data so the
 * next triangle of the strip can reuse it. */
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

static void __nakas_make_dummy(unsigned int **gpp)
{
    static const unsigned int pkt[0x5c] = {
        0x10000016, 0, 0, 0, 0, 0, 0, 0x6c05c100, 0x8002, 0x10004000, 0xe, 0, 0x60, 0, 0x14, 0,
        0, 0, 7, 0, 0x8001, 0x10004000, 0xe, 0, 0, 0, 7, 0, 0, 0, 0, 0x6c0e8000,
        0x8003, 0x30324000, 0x412, 0, 0x8003, 0x31324000, 0x412, 0, 0, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000,
        0, 0, 0x3f800000, 0x8000, 0, 0, 0, 0, 0, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000,
        0, 0, 0x3f800000, 0x8000, 0, 0, 0, 0, 0, 0, 0, 0x3f800000, 0, 0, 0, 0x3f800000,
        0, 0, 0x3f800000, 0x8000, 0, 0, 0, 0, 0, 0, 0, 0x13000000,
    };
    memcpy(*gpp, pkt, sizeof pkt);
    *gpp += 0x5c;
}

/* Closes the batch whose tag was reserved at *DMATagp (vtx_num vertices written since), writes the MSCAL/MSCNT
 * qword at *gpp and reserves the next tag after it. vtx_num == 0: only reserves the tag space. */
static void __nakas_make_tag(unsigned int **gpp, unsigned int **DMATagp, unsigned int *texp, unsigned int *prim,
                             int vtx_num, int *first)
{
    unsigned int *t = *DMATagp, *g = *gpp;
    if (!vtx_num) {
        *gpp = g + 0x28;
        return;
    }
    t[0] = (vtx_num * 4 + 10) | 0x10000000;   /* DMA cnt */
    t[1] = 0; t[2] = 0; t[3] = 0;
    memcpy(t + 4, texp, 0x60);
    t[0x1c] = 0; t[0x1d] = 0; t[0x1e] = 0;
    t[0x1f] = ((vtx_num * 4 + 2) << 16) | 0x6c008000;   /* UNPACK V4-32 */
    unsigned long long n = (unsigned long long)(long long)vtx_num, *p = (unsigned long long *)prim;
    unsigned long long *q = (unsigned long long *)(t + 0x20);
    q[0] = (p[2] & ~0x7fffULL) | n;   /* GIF tags with NLOOP = vtx_num */
    q[1] = p[3];
    q[2] = (p[4] & ~0x7fffULL) | n;
    q[3] = p[5];
    g[0] = 0; g[1] = 0; g[2] = 0;
    if (*first) {
        g[3] = 0x14000200;   /* MSCAL 0x200 */
        *first = 0;
    } else {
        g[3] = 0x17000000;   /* MSCNT */
    }
    *DMATagp = g + 4;
    *gpp = g + 0x2c;
}

/* Vertex where edge vtx1->vtx2 crosses the plane (plane distances fin1, fin2): v1 + (v2 - v1) * t as ACC = v1*1 -
 * v1*t, + v2*t (pos / normal / st .xyz, w kept from vtx1; rgba .xyzw); normal renormalised, st = (s, t, 1, 0). The
 * vertex goes to *gpp; SW != 0 also keeps it in cross_data. pln (a1) is passed by the caller but never read
 * (protofix). */
static void __nakas_mkdl_cross_point_Mk3(unsigned int **gpp, float *pln, float *vtx1, float *vtx2, float fin1,
                                         float fin2, int SW)
{
    PS2_FRAME(0xb0);
    (void)pln;
    const float *a = vtx1, *b = vtx2;
    float t = ps2_div(-fin1, fin2 - fin1);
    float v[4][4] __attribute__((aligned(16)));
    for (int q = 0; q < 4; q++) {
        int n = q == 3 ? 4 : 3;
        for (int i = 0; i < 4; i++) {
            if (i < n) {
                float acc = a[q * 4 + i] * 1.0f;
                acc = acc - a[q * 4 + i] * t;
                v[q][i] = acc + b[q * 4 + i] * t;
            } else {
                v[q][i] = a[q * 4 + i];
            }
        }
    }
    sceVu0Normalize(v[1], v[1]);
    v[2][2] = 1.0f;
    v[2][3] = 0.0f;
    memcpy(*gpp, v, 64);
    *gpp += 0x10;
    if (SW) memcpy(cross_data, v, 64);
}

/* Plane distance of ct vertices (stride 0x40): zfp[i] = dot4(vtx, pln), zp[i] = (zfp[i] >= 0); returns the count
 * of vertices on the positive side. Groups of 4 use the VU0 sum orders of the original. */
static int __vu0_pre_proc(char *zp, float *zfp, float *vtx, float *pln, int ct)
{
    int n = 0;
    for (; ct >= 4; ct -= 4, vtx += 0x40) {
        float m[4][4];
        for (int k = 0; k < 4; k++)
            for (int i = 0; i < 4; i++) m[k][i] = vtx[k * 16 + i] * pln[i];
        zfp[0] = ((m[0][0] + m[0][1]) + m[0][2]) + m[0][3];
        zfp[1] = ((m[1][1] + m[1][0]) + m[1][2]) + m[1][3];
        zfp[2] = ((m[2][2] + m[2][0]) + m[2][1]) + m[2][3];
        zfp[3] = ((m[3][3] + m[3][0]) + m[3][1]) + m[3][2];
        for (int k = 0; k < 4; k++, zp++, zfp++) {
            if (*zfp < 0.0f) *zp = 0;
            else { *zp = 1; n++; }
        }
    }
    while (ct--) {
        float d = pln[3] + ((pln[0] * vtx[0] + pln[1] * vtx[1]) + pln[2] * vtx[2]);
        *zfp = d;
        if (d < 0.0f) *zp = 0;
        else { *zp = 1; n++; }
        zp++; zfp++; vtx += 0x10;
    }
    return n;
}

int DF_MkDL_ClipModel(unsigned int **gpp, unsigned int *omd, int pnum, float *pln, int last_flag)
{
    PS2_FRAME(0x150);
    unsigned int *out = *gpp;
    int total = 0, first = 1;
    while (pnum-- != 0) {
        unsigned int *texp = omd + 4, *prim = omd + 0x1c;
        float *vtx = (float *)(omd + 0x28);
        int ct = omd[0x20] & 0x7fff;
        if (__vu0_pre_proc(z_in, z_fin, vtx, pln, ct) > 0) {
            int tags = -1, vtx_num = 0, have_cross = 0, cont = 0;
            unsigned int *gp = out, *tagp = out;
            const char *zi = z_in;
            const float *zf = z_fin;
            int rem = ct - 2;
            while (rem-- != 0) {
                float *v0 = vtx, *v1 = vtx + 0x10, *v2 = vtx + 0x20;
                if (rem >= 2 && (((unsigned int *)v2)[0xb] & 0x8000)) {   /* strip restart: skip 2 */
                    vtx += 0x20; zf += 2; zi += 2;
                    have_cross = 0; cont = 0;
                    rem--;
                    continue;
                }
                char in0 = zi[0], in1 = zi[1];
                char sum = (char)(zi[2] + (in0 + in1));
                if (sum == 0) {
                    have_cross = 0; cont = 0;
                } else if (sum == 3) {
                    if (cont == 1) {
                        DF_VU0_Copy4QW(gp, v2);
                        vtx_num++;
                    } else {
                        __nakas_make_tag(&gp, &tagp, texp, prim, vtx_num, &first);
                        DF_VU0_Copy4QW(gp, v0); gp += 0x10;
                        DF_VU0_Copy4QW(gp, v1); gp += 0x10;
                        DF_VU0_Copy4QW(gp, v2);
                        vtx_num = 3;
                        tags++;
                    }
                    gp += 0x10;
                    have_cross = 0; cont = 1;
                } else if (sum == 2) {
                    __nakas_make_tag(&gp, &tagp, texp, prim, vtx_num, &first);
                    if (!in0) {          /* v0 behind */
                        if (have_cross) { DF_VU0_Copy4QW(gp, cross_data); gp += 0x10; }
                        else __nakas_mkdl_cross_point_Mk3(&gp, pln, v0, v1, zf[0], zf[1], 0);
                        DF_VU0_Copy4QW(gp, v1); gp += 0x10;
                        __nakas_mkdl_cross_point_Mk3(&gp, pln, v2, v0, zf[2], zf[0], 0);
                        DF_VU0_Copy4QW(gp, v2); gp += 0x10;
                        have_cross = 0;
                    } else if (!in1) {   /* v1 behind */
                        DF_VU0_Copy4QW(gp, v0); gp += 0x10;
                        if (have_cross) { DF_VU0_Copy4QW(gp, cross_data); gp += 0x10; }
                        else __nakas_mkdl_cross_point_Mk3(&gp, pln, v0, v1, zf[0], zf[1], 0);
                        DF_VU0_Copy4QW(gp, v2); gp += 0x10;
                        __nakas_mkdl_cross_point_Mk3(&gp, pln, v1, v2, zf[1], zf[2], 1);
                        have_cross = 1;
                    } else {             /* v2 behind */
                        DF_VU0_Copy4QW(gp, v0); gp += 0x10;
                        DF_VU0_Copy4QW(gp, v1); gp += 0x10;
                        __nakas_mkdl_cross_point_Mk3(&gp, pln, v2, v0, zf[2], zf[0], 0);
                        __nakas_mkdl_cross_point_Mk3(&gp, pln, v1, v2, zf[1], zf[2], 1);
                        have_cross = 1;
                    }
                    vtx_num = 4; tags++; cont = 0;
                } else {   /* sum == 1 */
                    __nakas_make_tag(&gp, &tagp, texp, prim, vtx_num, &first);
                    if (in0) {           /* only v0 in front */
                        DF_VU0_Copy4QW(gp, v0); gp += 0x10;
                        if (have_cross) { DF_VU0_Copy4QW(gp, cross_data); gp += 0x10; }
                        else __nakas_mkdl_cross_point_Mk3(&gp, pln, v0, v1, zf[0], zf[1], 0);
                        __nakas_mkdl_cross_point_Mk3(&gp, pln, v2, v0, zf[2], zf[0], 0);
                        have_cross = 0;
                    } else if (in1) {    /* only v1 */
                        if (have_cross) { DF_VU0_Copy4QW(gp, cross_data); gp += 0x10; }
                        else __nakas_mkdl_cross_point_Mk3(&gp, pln, v0, v1, zf[0], zf[1], 0);
                        DF_VU0_Copy4QW(gp, v1); gp += 0x10;
                        __nakas_mkdl_cross_point_Mk3(&gp, pln, v1, v2, zf[1], zf[2], 1);
                        have_cross = 1;
                    } else {             /* only v2 */
                        __nakas_mkdl_cross_point_Mk3(&gp, pln, v1, v2, zf[1], zf[2], 1);
                        DF_VU0_Copy4QW(gp, v2); gp += 0x10;
                        __nakas_mkdl_cross_point_Mk3(&gp, pln, v2, v0, zf[2], zf[0], 0);
                        have_cross = 1;
                    }
                    vtx_num = 3; tags++; cont = 0;
                }
                if (rem == 0) {   /* end of strip: close the batch, drop the reserved tag */
                    __nakas_make_tag(&gp, &tagp, texp, prim, vtx_num, &first);
                    tags++;
                    gp -= 0x28;
                }
                vtx += 0x10; zf++; zi++;
            }
            total += tags;
            out = gp;
        }
        omd += ct * 0x10 + 0x2c;
    }
    if (last_flag && total) {
        __nakas_make_dummy(&out);
        total++;
    }
    *gpp = out;
    return total;
}
