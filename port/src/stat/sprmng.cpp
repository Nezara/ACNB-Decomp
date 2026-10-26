/* src/stat/sprmng.cpp - sprite scene manager (whole file): a chunk file of scenes -> objects -> parts -> sprites
 * (the cockpit screen layout, initckpt.c). SPRMNG_SetSceneNo copies a scene's objects into the live object list,
 * SPRMNG_Draw draws every live object's sprites (parts with param bit 0 use the master colour, object alpha). */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#define A16 __attribute__((aligned(16)))

#ifdef ACNB_SOURCEPORT
char *cnk_id[6] = {(char *)"MHDR", (char *)"MSCN", (char *)"MOBJ", (char *)"MPRT", (char *)"MSPR", (char *)"MEND"};   /* 0x265b60 */
#endif

static void draw_object(struct_3e127a03 *mng, struct_fb2d6b13 *obj)
{
    PS2_FRAME(0xc0);
    struct_1175f2ab *prt = &mng->prt[obj->idx_parts];
    struct_9f301495 *spr = &mng->spr[prt->sta_spr];
    for (int i = 0; i < prt->num_spr; i++, spr++) {
        int c[4] A16;
        int *rgba = obj->rgba;
        if (prt->param & 1) {
            memcpy(c, mng->mcolor, 16);
            c[3] = obj->rgba[3];
            rgba = c;
        }
        int x = obj->ulx + spr->ulx;
        int y = obj->uly + spr->uly;
        float u = (float)spr->uls;
        float v = (float)spr->ult;
        ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 6), 6); GS_NewList();
        struct_f386fb29 t;
        memcpy(&t, &spr->texinfo.tex0, 8);
        GS_SetTEX0_1(t);
        GS_SetRGBA(rgba);
        GS_SetBlendMode(spr->blend);
        GS_DrawSpriteD(x, y, x + spr->wd, y + spr->ht, u, v, u + (float)spr->wd, v + (float)spr->ht, spr->flip);
        GS_FlushList(); ps2_rq_tag_end();
    }
}

static void draw_scene(struct_3e127a03 *mng)
{
    PS2_FRAME(0x40);
    struct_fb2d6b13 *obj = mng->objlist;
    for (int i = 0; i < mng->objnum; i++, obj++) draw_object(mng, obj);
}

/* chunk handler (CNK_CreateParser): cnk_id order = header, scenes, objects, parts, sprites, end */
static int parse_data(int cnkid, void *data, void *paramp)
{
    struct_3e127a03 *mng = (struct_3e127a03 *)paramp;
    switch (cnkid) {
    case 0: mng->hdr = (struct_bbe0712e *)data; break;
    case 1: mng->scn = (struct_7634bc2a *)data; break;
    case 2: mng->obj = (struct_c9646ebf *)data; break;
    case 3: mng->prt = (struct_1175f2ab *)data; break;
    case 4: mng->spr = (struct_9f301495 *)data; break;
    case 5: return 0;
    }
    return 1;
}

void SPRMNG_SetMasterColor(struct_3e127a03 *mng, int *rgba)
{
    if (rgba) memcpy(mng->mcolor, rgba, 16);
}

void SPRMNG_Draw(struct_3e127a03 *mng)
{
    PS2_FRAME(0x20);
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 6), 6); GS_NewList();
    GS_SetTEXFLUSH();
    GS_SetTEST_1(0, 0, 0, 0, 0, 0, 1, 1);
    GS_SetCLAMP_1(1, 1, 0, 0, 0, 0);
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    GS_FlushList(); ps2_rq_tag_end();
    draw_scene(mng);
}

static void copy_object(struct_fb2d6b13 *d, const struct_c9646ebf *s)
{
    d->rgba[0] = s->rgba[0];
    d->rgba[1] = s->rgba[1];
    d->rgba[2] = s->rgba[2];
    d->rgba[3] = s->rgba[3];
    d->ulx = s->ulx;
    d->uly = s->uly;
    d->idx_parts = s->idx_parts;
}

/* 1 when the live list was reloaded (scene changed and update != 0) */
int SPRMNG_SetSceneNo(struct_3e127a03 *mng, int scno, int update)
{
    if (scno >= mng->hdr->num_scene) return 0;
    if (mng->curscn == scno) return 0;
    mng->curscn = scno;
    if (!update) return 0;
    struct_7634bc2a *scn = &mng->scn[scno];
    struct_c9646ebf *src = &mng->obj[scn->sta_object];
    struct_fb2d6b13 *dst = mng->objlist;
    int i = 0;
    int num = scn->num_object;
    for (; i < num; i++) {
        copy_object(dst++, src++);
        num = scn->num_object;
    }
    mng->objnum = (short)num;
    return 1;
}

/* relocate the sprites' texture / CLUT base pointers that fall in [start, end) (per-player VRAM pages) */
void SPRMNG_OffsetTEX(struct_3e127a03 *mng, int ofsTBP, int ofsCBP, int ofsStartTBP, int ofsEndTBP, int ofsStartCBP,
                      int ofsEndCBP)
{
    struct_9f301495 *spr = mng->spr;
    for (int i = 0; i < mng->hdr->num_spr; i++, spr++) {
        unsigned short lo;
        memcpy(&lo, &spr->texinfo.tex0, 2);
        int tbp = lo & 0x3fff;
        if (ofsStartTBP <= tbp && tbp < ofsEndTBP) {
            lo = (unsigned short)((lo & 0xc000) | ((tbp + ofsTBP) & 0x3fff));
            memcpy(&spr->texinfo.tex0, &lo, 2);
        }
        unsigned long long t;
        memcpy(&t, &spr->texinfo.tex0, 8);
        unsigned long long cbp = (t >> 37) & 0x3fff;
        if (cbp >= (unsigned long long)(long long)ofsStartCBP && cbp < (unsigned long long)(long long)ofsEndCBP) {
            t = (t & 0xfff8001fffffffffull) | (((cbp + (long long)ofsCBP) & 0x3fff) << 37);
            memcpy(&spr->texinfo.tex0, &t, 8);
        }
    }
}

/* live object objid = object mobjid of scene mscnid */
void SPRMNG_CopyObject(struct_3e127a03 *mng, int objid, int mscnid, int mobjid)
{
    if (mscnid >= mng->hdr->num_scene) return;
    copy_object(&mng->objlist[objid], &mng->obj[mng->scn[mscnid].sta_object + mobjid]);
}

void SPRMNG_Init(struct_3e127a03 *mng, void *data)
{
    PS2_FRAME(0x40);
    struct_1b9d99a9 psr A16;
    memset(mng, 0, 0x50);
    CNK_CreateParser(&psr, cnk_id, 6, GFN(parse_data), mng);
    CNK_ParseData(&psr, data);
    mng->curscn = -1;
    mng->objlist = (struct_fb2d6b13 *)Memm_Alloc(1, 0x200, 0x10);
    mng->objnum = 0;
}
