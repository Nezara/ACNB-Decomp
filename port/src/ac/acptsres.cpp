/* src/ac/acptsres.cpp - per-AC parts resource table: AcPtsResSt -> AcPtsResPrivateSt holding one sfx combination
 * file (_stSfxCombHead) per parts slot psr (0..12) plus the monoeye textures. The getters validate psr, fetch the
 * comb head and index its tables (weapon / spell / magic / icon data); RegisterWsfx relocates a freshly loaded comb
 * file in place (offsets -> PS2 addresses). Parts without a comb file return NULL. */
#include "acnb/sdk.h"

static inline _stSfxCombHead *ptsres_comb(AcPtsResSt *ptsresp, int psr)
{
    return ptsresp->prvp->combp[psr];
}

unsigned char *AcPtsRes_GetMonoeyeTm2(AcPtsResSt *ptsresp) { return ptsresp->prvp->metm2p; }
unsigned char *AcPtsRes_GetMonoeyeTp2(AcPtsResSt *ptsresp) { return ptsresp->prvp->metp2p; }
void AcPtsRes_RegisterMonoeyeTm2(AcPtsResSt *ptsresp, char *resp) { ptsresp->prvp->metm2p = (unsigned char *)resp; }
void AcPtsRes_RegisterMonoeyeTp2(AcPtsResSt *ptsresp, char *resp) { ptsresp->prvp->metp2p = (unsigned char *)resp; }

/* magic order n (reduced-magic mode: the table entry after it; no upper bound on order) */
_magic2_data_st *AcPtsRes_GetMagicPtr(AcPtsResSt *ptsresp, int psr, int order)
{
    PS2_FRAME(0x30);
    if (psr < 0 || psr >= 13) return 0;
    _stSfxCombHead *c = ptsres_comb(ptsresp, psr);
    if (!c) return 0;
    if (order < 0) return 0;
    if (DF_MAGIC2_GetMagicReduce()) return (_magic2_data_st *)c->offset[order + 1];
    return (_magic2_data_st *)c->offset[order];
}

_stSpell2Data *AcPtsRes_GetSpellPtr(AcPtsResSt *ptsresp, int psr, int order)
{
    if (psr < 0 || psr >= 13) return 0;
    _stSfxCombHead *c = ptsres_comb(ptsresp, psr);
    if (!c) return 0;
    if (order < 0) return 0;
    return (_stSpell2Data *)c->offset[order];
}

/* weapon magic record index (0x140 bytes each; a negative index is not rejected) */
_magic2_data_st *AcPtsRes_GetWeaponMagicPtr(AcPtsResSt *ptsresp, int psr, int index)
{
    PS2_FRAME(0x30);
    if (psr < 0 || psr >= 13) return 0;
    _stSfxCombHead *c = ptsres_comb(ptsresp, psr);
    if (!c) return 0;
    if (DF_MAGIC2_GetMagicReduce()) {
        int n = c->magic_num2;
        if (n <= 0) return 0;
        if (index >= n) return 0;
        return (_magic2_data_st *)(c->magic_ofs2 + index * 0x140);
    }
    int n = c->magic_num;
    if (n <= 0) return 0;
    if (index >= n) return 0;
    return (_magic2_data_st *)(c->magic_ofs + index * 0x140);
}

/* weapon spell record index (0xc0 bytes each; a negative index is not rejected) */
_stSpell2Data *AcPtsRes_GetWeaponSpellPtr(AcPtsResSt *ptsresp, int psr, int index)
{
    if (psr < 0 || psr >= 13) return 0;
    _stSfxCombHead *c = ptsres_comb(ptsresp, psr);
    if (!c) return 0;
    int n = c->spell_num;
    if (n <= 0) return 0;
    if (index >= n) return 0;
    return (_stSpell2Data *)(c->spell_ofs + index * 0xc0);
}

unsigned char *AcPtsRes_GetWeaponIcon(AcPtsResSt *ptsresp, int psr)
{
    if (psr < 0 || psr >= 13) return 0;
    _stSfxCombHead *c = ptsres_comb(ptsresp, psr);
    if (!c) return 0;
    if (c->icon_num <= 0) return 0;
    return (unsigned char *)c->icon_ofs;
}

/* weapon parameters of order n (no bound / sign check on order) */
PartsParamWeaponSt *AcPtsRes_GetWeaponPtr(AcPtsResSt *ptsresp, int psr, int order)
{
    if (psr < 0 || psr >= 13) return 0;
    _stSfxCombHead *c = ptsres_comb(ptsresp, psr);
    if (!c) return 0;
    if (c->weapon_num <= 0) return 0;
    return (PartsParamWeaponSt *)c->offset[order];
}

_stSfxCombHead *AcPtsRes_GetSfxCombHead(AcPtsResSt *ptsresp, int psr)
{
    if (psr < 0 || psr >= 13) return 0;
    return ptsres_comb(ptsresp, psr);
}

/* swap in an already relocated comb file */
void AcPtsRes_ReRegisterWsfx(AcPtsResSt *ptsresp, int psr, char *resp)
{
    PS2_FRAME(0x10);
    PS2_STACK(char *, spill, 0xc);   /* MWCC spills resp to its frame */
    spill = resp;
    if (psr < 0 || psr >= 13) return;
    ptsresp->prvp->combp[psr] = (_stSfxCombHead *)spill;
}

/* register a freshly loaded comb file: table offsets -> PS2 addresses (offset[] entries below 0x40 = none) */
void AcPtsRes_RegisterWsfx(AcPtsResSt *ptsresp, int psr, char *resp)
{
    if (psr < 0 || psr >= 13) return;
    _stSfxCombHead *c = (_stSfxCombHead *)resp;
    ptsresp->prvp->combp[psr] = c;
    if (!c) return;
    unsigned int base = (unsigned int)(uintptr_t)resp;
    if (c->weapon_num > 0) c->weapon_ofs += base;
    if (c->spell_num > 0) c->spell_ofs += base;
    if (c->magic_num > 0) c->magic_ofs += base;
    if (c->magic_num2 > 0) c->magic_ofs2 += base;
    if (c->icon_num > 0) c->icon_ofs += base;
    for (int k = 0; k < 8; k++) {
        if (c->offset[k] < 0x40) c->offset[k] = 0;
        else c->offset[k] += base;
    }
}

int AcPtsRes_Init(AcPtsResSt *ptsresp)
{
    PS2_FRAME(0x30);
    if (!ptsresp) return 0;
    AcPtsResPrivateSt *pdest = ptsresp->prvp;
    ZeroMemory8b(ptsresp, 4);
    ZeroMemory8b(pdest, 0x3c);
    ptsresp->prvp = pdest;
    return 1;
}
