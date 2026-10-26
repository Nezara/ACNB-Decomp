/* src/model/tp2.cpp - model file readers (whole file): fix up the offsets in a loaded HMD (hit mesh), OMD (VU1
 * model: table of primitive blocks per object + object table + VU1 data) or TP2 (polygon model) image in place
 * and hook it to the StModel. OMD images carry a relocation marker at +8: -1 = offsets not yet relocated
 * (overwritten with 0xeeeeeeee once done). The ExtMem variants copy several OMDs into one shared table / VU1 area
 * (OmdMemSetSt = the next free slots). */
#include "acnb/sdk.h"

/* HMD: polygon list (each: 0x20-byte header + pv_num 16-byte vertex refs whose index becomes a pointer to the
 * transformed vertex in scratchpad at 0x100 + index * 16), then v_num vertices and n_num normals. */
void *DF_ReadHMDPointer(char *bufp, StModel *mp)
{
    mp->hmd = (HmdHeadSt *)bufp;
    char *p = bufp + 0x20;
    mp->hmd->php = (struct_de4d68f5 *)p;
    unsigned int npoly = mp->hmd->p_num;
    unsigned char *poly = (unsigned char *)mp->hmd->php;
    const unsigned int spr = PS2PTR(PS2_SPR(0x100));
    for (unsigned int i = 0; i < npoly; i++) {
        unsigned int nv = *(unsigned short *)poly;
        unsigned char *ref = poly + 0x20;
        p += 0x20;
        for (unsigned int k = 0; k < nv; k++, ref += 0x10) *(unsigned int *)ref = (*(unsigned int *)ref << 4) + spr;
        p += nv << 4;
        poly = ref;
    }
    mp->hmd->vp = (float (*)[4])p;
    p += mp->hmd->v_num << 4;
    mp->hmd->np = (float (*)[4])p;
    return p + (mp->hmd->n_num << 4);
}

/* offsets of the primitive blocks -> pointers, once */
static void omd_relocate(char *bufp, OmdTableSt *t, int n)
{
    if (*(int *)(bufp + 8) != -1) return;
    for (int i = 0; i < n; i++) t[i].mstart = PS2PTR(bufp + t[i].mstart);
    memset(bufp + 8, 0xee, 4);
}

/* low-detail OMD of a model */
void *DF_ReadOMDPointerLowEnd(char *bufp, StModel *mp)
{
    OmdTableSt *tbl = (OmdTableSt *)(bufp + 0x10);
    mp->nobj = (unsigned short)*(unsigned int *)bufp;
    mp->nmobj = mp->nobj;
    mp->kind |= 0x8000;
    mp->omdtablelw = tbl;
    mp->otp = (_objtbl *)(tbl + mp->nobj);
    mp->vu1mplw = (unsigned int *)((_objtbl *)(tbl + mp->nobj) + mp->nobj);
    omd_relocate(bufp, mp->omdtablelw, mp->nobj);
    return bufp + *(int *)(bufp + 4);
}

/* Appends one OMD to the shared area (isfirst: the model starts there). Block / matrix offsets are relative to
 * the VU1 data, which follows the 0x10 header + 0x60 bytes of table / object entry per object. */
static char *ReadOmdPointer_ExtMem(char *bufp, StModel *mp, OmdMemSetSt *omdmemp, int isfirst)
{
    unsigned int n = *(unsigned int *)bufp;
    int size = *(int *)(bufp + 4);
    int hdr = (int)(n * 0x60 + 0x10);
    if (!isfirst) {
        mp->nobj = (unsigned short)(mp->nobj + n);
        mp->nmobj = (unsigned short)(mp->nmobj + n);
        omdmemp->vu1mp -= 4;   /* (the VU1 data of consecutive OMDs overlaps by one qword) */
    } else {
        mp->kind |= 0x8000;
        mp->omdtable = omdmemp->omdtable;
        mp->otp = omdmemp->otp;
        mp->vu1mp = omdmemp->vu1mp;
        mp->nobj = (unsigned short)n;
        mp->nmobj = (unsigned short)n;
    }
    if (omdmemp->omdtable != (OmdTableSt *)(bufp + 0x10))
        CopyMemory32b(omdmemp->omdtable, bufp + 0x10, (int)((n & 0xfffffffu) << 2));
    for (int i = 0; i < (int)n; i++) {
        OmdTableSt *t = &omdmemp->omdtable[i];
        t->matrix = PS2PTR(omdmemp->vu1mp) + (t->matrix - hdr);
        t->mstart = PS2PTR(omdmemp->vu1mp) + (t->mstart - hdr);
    }
    omdmemp->omdtable += n;
    char *ot = bufp + *(int *)bufp * 0x10 + 0x10;
    if (omdmemp->otp != (_objtbl *)ot) CopyMemory32b(omdmemp->otp, ot, (int)((n * 5 & 0xfffffffu) << 2));
    omdmemp->otp += n;
    unsigned int rest = (unsigned int)(size - hdr);
    char *vu = bufp + *(int *)bufp * 0x60 + 0x10;
    if (omdmemp->vu1mp != (unsigned int *)vu) CopyMemory32b(omdmemp->vu1mp, vu, (int)(rest >> 2));
    omdmemp->vu1mp = (unsigned int *)((char *)omdmemp->vu1mp + rest);
    return bufp + size;
}

char *tp2_ReadOmdPointer_ExtMemAdd(char *bufp, StModel *mp, OmdMemSetSt *omdmemp)
{
    return ReadOmdPointer_ExtMem(bufp, mp, omdmemp, 0);
}

char *tp2_ReadOmdPointer_ExtMemFirst(char *bufp, StModel *mp, unsigned int anim, OmdMemSetSt *omdmemp)
{
    if (!anim) mp->op = MDL_AllocObject(*(int *)bufp);
    return ReadOmdPointer_ExtMem(bufp, mp, omdmemp, 1);
}

unsigned int tp2_GetOmdVu1mpOffset(char *bufp) { return (unsigned int)(*(int *)bufp * 0x60 + 0x10); }

/* anim = 0: the model gets new objects posed from the object table */
void *DF_ReadOMDPointer(char *bufp, StModel *mp, unsigned int anim)
{
    mp->nobj = (unsigned short)*(unsigned int *)bufp;
    mp->nmobj = mp->nobj;
    if (!anim) mp->op = MDL_AllocObject(mp->nobj);
    OmdTableSt *tbl = (OmdTableSt *)(bufp + 0x10);
    mp->kind |= 0x8000;
    mp->omdtable = tbl;
    mp->otp = (_objtbl *)(tbl + mp->nobj);
    mp->vu1mp = (unsigned int *)((_objtbl *)(tbl + mp->nobj) + mp->nobj);
    omd_relocate(bufp, mp->omdtable, mp->nobj);
    if (!anim) {
        for (int i = 0; i < mp->nobj; i++) {
            memcpy(mp->op[i].position, mp->otp[i].position, 16);
            memcpy(mp->op[i].angle, mp->otp[i].angle, 16);
        }
    }
    return bufp + *(int *)(bufp + 4);
}

/* TP2: object table, then per object its polygons (0x20 header + pv_num 0x20-byte vertices), vertices, normals */
void *DF_ReadTP2Pointer(char *bufp, StModel *mp, unsigned int anim)
{
    mp->nobj = (unsigned short)*(unsigned int *)(bufp + 8);
    mp->nmobj = mp->nobj;
    if (!anim) mp->op = MDL_AllocObject(mp->nobj);
    mp->otp = (_objtbl *)(bufp + 0x10);
    unsigned char *p = (unsigned char *)(bufp + 0x10) + mp->nobj * (int)sizeof(_objtbl);
    for (int i = 0; i < mp->nmobj; i++) {
        _objtbl *ot = &mp->otp[i];
        if (!anim) {
            memcpy(mp->op[i].position, mp->otp[i].position, 16);
            memcpy(mp->op[i].angle, mp->otp[i].angle, 16);
        }
        ot->php = (struct_de4d68f5 *)p;
        int np = mp->otp[i].p_num;
        for (int k = 0; k < np; k++) p += 0x20 + (*(unsigned short *)p << 5);
        ot->vp = (float (*)[4])p;
        p += ot->v_num << 4;
        ot->np = (float (*)[4])p;
        p += ot->n_num << 4;
    }
    return p;
}
