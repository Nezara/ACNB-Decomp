/* src/menu/mtex.cpp — the menu's texture registry (OLMENU) on top of lib/texmng: MenuTexMng is the one TexMng
 * instance (pages 0x15e.., 0x7a pages, 8 clut pages). A texture is addressed by a 32-bit id (kind = id >> 16,
 * index = id & 0xffff):
 *  - texture groups: a loaded binder image (MenuTex_RegisterBndTexture) becomes one of up to 32 groups; its ids are
 *    the binder's file ids (searched newest group first);
 *  - single textures: kind 0xb, 64 slots registered from a loose texture head;
 *  - category tables (kinds 0, 1 = Jfont, 2 = Control, 8, 9, 10): menu data entries registered per category.
 * MenuTex_StandbyTexture resolves an id, copies the head's TEX0 / TEX1 preset into the caller's texinfo and makes
 * the texture (and its clut) resident. */
#include <string.h>
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
int s_anMaxIdInCategory[12] = {0x1c, 0x13, 0xf, 0, 0, 0, 0, 0, 0x2d, 0x20, 0xf, 0};
#endif

/* copies the head's 16-byte TEX0 / TEX1 preset (two ld / sd) */
static inline void mtex_copy_preset(struct struct_13efd7f4 *dst, const struct struct_c53f69de *head)
{
    memcpy(dst, &head->tex, 0x10);
}

/* group search shared by StandbyTexture / GetTexHeadSt: newest group first; a group is skipped when id is outside
 * [first id, last id] of its binder table (unsigned; reads ids[-1] for an empty group), then scanned linearly */
static struct struct_7c527b8c *mtex_find_in_groups(int id)
{
    for (int g = s_TexContainer.nNumGroup - 1; g >= 0; g--) {
        struct struct_fe913b56 *grp = &s_TexContainer.aTexGroup[g];
        struct StFileInfo *ids = grp->BinderMng.file_infop;
        int cnt = (int)grp->BinderMng.head->file_num;
        struct struct_7c527b8c *ent = NULL;
        if (!((unsigned int)id < ids[0].file_id) && !((unsigned int)ids[cnt - 1].file_id < (unsigned int)id)) {
            for (int i = 0; i < cnt; i++) {
                if ((int)ids[i].file_id == id) {
                    ent = (struct struct_7c527b8c *)((char *)grp->pTexInfo + i * 0x10);
                    break;
                }
            }
        }
        if (ent) return ent;
    }
    return NULL;
}

int MenuTex_StandbyTexture(int id, struct struct_13efd7f4 *pTexInfo)
{
    PS2_FRAME(0x50);
    int cat = (id >> 16) & 0xffff;
    if (id == -1) return 0;

    struct struct_7c527b8c *ent = mtex_find_in_groups(id);
    if (ent) {
        mtex_copy_preset(pTexInfo, ent->pHead);
        if (TexMng_Begin(&MenuTexMng) > 0) {
            TexMng_StandbyTexture(&MenuTexMng, ent->hTex, (unsigned long long *)pTexInfo);
            if (ent->hClut) TexMng_StandbyClut(&MenuTexMng, ent->hClut, (unsigned long long *)pTexInfo);
            TexMng_End(&MenuTexMng);
        }
        return 1;
    }
    if (__MenuTex_StandbySingleTextureId(id, pTexInfo)) return 1;

    if (cat != 10 && cat != 9 && cat != 8 && cat != 2 && cat != 1 && cat != 0) return 0;
    int idx = id & 0xffff;
    if (idx >= s_anMaxIdInCategory[cat]) return 0;
    struct struct_fb017f0d *cent = &s_aTexInfo[cat][idx];
    struct struct_c53f69de *head = cent->pHead;
    if (!head) return 0;
    TexMng_Begin(&MenuTexMng);   /* result ignored on this path */
    mtex_copy_preset(pTexInfo, head);
    TexMng_StandbyTexture(&MenuTexMng, cent->hTex, (unsigned long long *)pTexInfo);
    if (cent->hClut) TexMng_StandbyClut(&MenuTexMng, cent->hClut, (unsigned long long *)pTexInfo);
    TexMng_End(&MenuTexMng);
    return 1;
}

/* RegisterTex request of a texture head: {psm, 1 << TW, 1 << TH, pixels at head + 0x20}, rest zero */
static void mtex_tex_req(struct tagTEXMNG_TEXINFO *req, const struct struct_c53f69de *head)
{
    memset(req, 0, sizeof *req);
    req->format = (int)head->tex.tex0.psm;
    req->width = 1 << (int)head->tex.tex0.tw;
    req->height = 1 << (int)head->tex.tex0.th;
    req->pdata = (char *)head + 0x20;
}

/* RegisterClut request: {psm, cpsm, clut at head + 0x20 + head->wnum} */
static void mtex_clut_req(struct tagTEXMNG_CLUTINFO *req, const struct struct_c53f69de *head)
{
    req->format = (int)head->tex.tex0.psm;
    req->cformat = (int)head->tex.tex0.cpsm;
    req->pdata = (char *)head + 0x20 + head->wnum;
}

static int __MenuTex_InitGroup(struct struct_fe913b56 *pGroup, void *pFileImage)
{
    PS2_FRAME(0xe0);
    Binder_Init(&pGroup->BinderMng, pFileImage, 0);
    int n = (int)pGroup->BinderMng.head->file_num;
    pGroup->pTexInfo = (struct struct_7c527b8c *)DF_MMM_Alloc(0, n * 0x10);
    pGroup->pBndData = pFileImage;
    ZeroMemory(pGroup->pTexInfo, n * 0x10);
    for (int i = 0; i < n; i++) {
        struct StFileInfo *fi = &pGroup->BinderMng.file_infop[i];
        struct struct_c53f69de *head = (struct struct_c53f69de *)((char *)pFileImage + fi->data_offset);
        struct tagTEXMNG_TEXINFO req;
        struct tagTEXMNG_CLUTINFO creq;
        unsigned int hTex = 0, hClut = 0;
        mtex_tex_req(&req, head);
        unsigned int out = 0;
        if (TexMng_RegisterTex(&MenuTexMng, &req, &out) > 0) hTex = out;
        pGroup->pTexInfo[i].hTex = hTex;
        memset(&creq, 0, sizeof creq);
        if (TexMng_IsClutFormat((int)head->tex.tex0.psm)) {
            mtex_clut_req(&creq, head);
            unsigned int out2 = 0;
            if (TexMng_RegisterClut(&MenuTexMng, &creq, &out2) > 0) hClut = out2;
        }
        pGroup->pTexInfo[i].hClut = hClut;
        pGroup->pTexInfo[i].pHead = head;
        pGroup->pTexInfo[i].id = fi->file_id;
    }
    return 1;
}

struct struct_c53f69de *MenuTex_GetTexHeadSt(int id)
{
    struct struct_7c527b8c *ent = mtex_find_in_groups(id);
    return ent ? ent->pHead : NULL;
}

int MenuTex_UnRegisterBndTexture(void *pFileImage)
{
    PS2_FRAME(0x80);
    if (!pFileImage) return 1;
    for (int g = s_TexContainer.nNumGroup - 1; g >= 0; g--) {
        struct struct_fe913b56 *grp = &s_TexContainer.aTexGroup[g];
        if (grp->pBndData != pFileImage) continue;
        int n = (int)grp->BinderMng.head->file_num;
        for (int i = 0; i < n; i++) {
            TexMng_UnRegisterTex(&MenuTexMng, grp->pTexInfo[i].hTex);
            TexMng_UnRegisterClut(&MenuTexMng, grp->pTexInfo[i].hClut);
        }
        if (grp->pTexInfo) {
            DF_MMM_Free(0, grp->pTexInfo);
            grp->pTexInfo = NULL;
        }
        /* close the gap: later groups move down one slot */
        for (; g < s_TexContainer.nNumGroup - 1; g++)
            memcpy(&s_TexContainer.aTexGroup[g], &s_TexContainer.aTexGroup[g + 1], sizeof(struct struct_fe913b56));
        s_TexContainer.nNumGroup = s_TexContainer.nNumGroup - 1;
        return 1;
    }
    return 0;
}

int MenuTex_RegisterBndTexture(void *pFileImage)
{
    PS2_FRAME(0x10);
    if (s_TexContainer.nNumGroup >= 0x20) return 0;
    __MenuTex_InitGroup(&s_TexContainer.aTexGroup[s_TexContainer.nNumGroup], pFileImage);
    s_TexContainer.nNumGroup++;
    return 1;
}

void MenuTex_InvalidateCache(void)
{
    PS2_FRAME(0x10);
    TexMng_InvalidateAllTexCache(&MenuTexMng);
    TexMng_InvalidateAllClutCache(&MenuTexMng);
}

static int __MenuTex_StandbySingleTextureId(int id, struct struct_13efd7f4 *pTexInfo)
{
    PS2_FRAME(0x30);
    int slot = id & 0xffff;
    if (((int)(id & 0xffff0000) >> 16) != 0xb || slot >= 0x40) return 0;
    struct struct_7c527b8c *ent = &s_SingleTex.aTexInfo[slot];
    if (ent->id != id) return 0;
    if (TexMng_Begin(&MenuTexMng) <= 0) return 0;
    mtex_copy_preset(pTexInfo, ent->pHead);
    TexMng_StandbyTexture(&MenuTexMng, ent->hTex, (unsigned long long *)pTexInfo);
    if (ent->hClut) TexMng_StandbyClut(&MenuTexMng, ent->hClut, (unsigned long long *)pTexInfo);
    TexMng_End(&MenuTexMng);
    return 1;
}

int MenuTex_UnRegisterSingleTex(int id)
{
    PS2_FRAME(0x20);
    int slot = id & 0xffff;
    if (id == 0) return 0;
    if (slot >= 0x40) return 0;
    struct struct_7c527b8c *ent = &s_SingleTex.aTexInfo[slot];
    if (ent->id != id) return 0;
    TexMng_UnRegisterTex(&MenuTexMng, ent->hTex);
    TexMng_UnRegisterClut(&MenuTexMng, ent->hClut);
    ent->id = 0;
    ent->hClut = 0;
    ent->hTex = 0;
    ent->pHead = NULL;
    return 1;
}

int MenuTex_RegisterSingleTex(struct struct_c53f69de *pTexHead)
{
    PS2_FRAME(0x60);
    int slot;
    for (slot = 0; slot < 0x40; slot++)
        if (s_SingleTex.aTexInfo[slot].id == 0) break;
    if (slot == 0x40) return -1;
    struct struct_7c527b8c *ent = &s_SingleTex.aTexInfo[slot];
    ent->id = slot | 0xb0000;   /* claimed before registering */
    struct tagTEXMNG_TEXINFO req;
    struct tagTEXMNG_CLUTINFO creq;
    mtex_tex_req(&req, pTexHead);
    unsigned int out = 0, out2 = 0;
    ent->hTex = TexMng_RegisterTex(&MenuTexMng, &req, &out) > 0 ? out : 0;
    memset(&creq, 0, sizeof creq);
    unsigned int hClut = 0;
    if (TexMng_IsClutFormat((int)pTexHead->tex.tex0.psm)) {
        mtex_clut_req(&creq, pTexHead);
        if (TexMng_RegisterClut(&MenuTexMng, &creq, &out2) > 0) hClut = out2;
    }
    ent->hClut = hClut;
    ent->pHead = pTexHead;
    return ent->id;
}

/* registers category `category`'s menu data textures; an entry whose RegisterTex fails keeps its old record, and a
 * non-clut format keeps the old hClut (IsClutFormat is asked twice, the second answer only guards a dead path) */
static void __MenuTex_RegisterCategory(int category, void *pdata)
{
    PS2_FRAME(0xe0);
    struct struct_fb017f0d *ent = s_aTexInfo[category];
    for (int k = 0; k < s_anMaxIdInCategory[category]; k++, ent++) {
        struct struct_c53f69de *head = (struct struct_c53f69de *)DF_MDATA_GetData(pdata, k);
        if (!head) continue;
        struct tagTEXMNG_TEXINFO req;
        mtex_tex_req(&req, head);
        unsigned int out = 0, h = 0;
        if (TexMng_RegisterTex(&MenuTexMng, &req, &out) > 0) h = out;
        if (!h) continue;
        ent->hTex = h;
        ent->pHead = head;
        if (!TexMng_IsClutFormat((int)head->tex.tex0.psm)) continue;
        struct tagTEXMNG_CLUTINFO creq;
        memset(&creq, 0, sizeof creq);
        unsigned int hClut = 0;
        if (TexMng_IsClutFormat((int)head->tex.tex0.psm)) {
            mtex_clut_req(&creq, head);
            unsigned int out2 = 0;
            if (TexMng_RegisterClut(&MenuTexMng, &creq, &out2) > 0) hClut = out2;
        }
        ent->hClut = hClut;
    }
}

/* drops category `category`'s textures (the count is re-read every iteration) */
static void mtex_unregister_category(int category)
{
    for (int k = 0; k < s_anMaxIdInCategory[category]; k++) {
        struct struct_fb017f0d *ent = &s_aTexInfo[category][k];
        TexMng_UnRegisterTex(&MenuTexMng, ent->hTex);
        TexMng_UnRegisterClut(&MenuTexMng, ent->hClut);
        ent->hTex = 0;
        ent->hClut = 0;
        ent->pHead = NULL;
    }
}

void MenuTex_UnRegisterControl(void)
{
    PS2_FRAME(0x30);
    mtex_unregister_category(2);
}

void MenuTex_RegisterControl(void)
{
    __MenuTex_RegisterCategory(2, MenuMng.tm2Tbl[2]);
}

void MenuTex_UnRegisterJfont(void)
{
    PS2_FRAME(0x30);
    mtex_unregister_category(1);
}

void MenuTex_RegisterJfont(void)
{
    __MenuTex_RegisterCategory(1, MenuMng.tm2Tbl[1]);
}

void MenuTex_UnRegisterCommon(void)
{
}

void MenuTex_RegisterCommon(void)
{
}

void MenuTex_Clear(void)
{
    PS2_FRAME(0x10);
    DF_MMM_Free(0, TexMng_Destroy(&MenuTexMng));
}

void MenuTex_Init(void)
{
    PS2_FRAME(0x10);
    int sz = TexMng_GetWorkBuffSize(0x7a, 8);
    TexMng_Create(&MenuTexMng, (char *)DF_MMM_Alloc(0, sz), 0x15e, 0x7a, 8);
    ZeroMemory(s_aTexInfo, 0x1c20);
    ZeroMemory(&s_TexContainer, 0x284);
    ZeroMemory(&s_SingleTex, 0x408);
}
