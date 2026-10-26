/* src/ac/acloader.cpp — loads an AC for a sortie: AcLoader_AllocAcBuffer (model + texture buffer per AC),
 * AcLoader_LoadAndSetup (StAcLoader on the stack -> parts params, body files core/head/arms/legs in three passes
 * (pass 0 = params + body model + body anim, 1 = shadow model, 2 = parts models, anims, textures, sfx), then
 * booster / inside / extensions / back and arm weapons), __SetupAcModel (TBP/CBP, emblem, texture upload, weapon icons).
 * Also: __SetupLoadPartsInfo (the 16-entry parts file table), __CatchFile / __CatchBodyFile (read a parts file, unpack
 * its header into g_pbufRead), __LoadAcPartsNoModel (FCS / generator / radiator params), __BringAnim (anim block into
 * the AC buffer + model flags), AcLoader_GetAnimIdFromCoreArmLeg, AcLoader_Init, __CopyPageClutPsmt4 (spare-weapon
 * CLUT blocks). Also AcLoader_ChangeIntoSpareWepTexture (spare weapon texture back in). Not native: AcLoader_RestoreImage. */

static inline char *align128(const void *p) { return (char *)(((unsigned int)p + 0x7f) & 0xffffff80u); }

/* TBP / CBP of this AC's texture pages (one shared page when AcOpt.txx is set) */
static inline int ac_tbp(int acid) { return AcOpt.txx == 0 ? acid * 0x400 - 0x540 : -0x540; }
static inline int ac_cbp(int acid) { return AcOpt.txx == 0 ? acid << 5 : 0; }

/* Current entry i of the parts file header (data pointer, size, attribute). */
static inline void ld_entry(StAcLoader *t, int i)
{
    StPartsDataHeader *h = t->m_pphead;
    t->m_pbufData = (char *)h + h->uiOffset[i];
    t->m_sizeData = h->uiSize[i];
    t->m_atrbData = h->ucAtrb[i];
}

/* Texture files of parts: entries idxStart.. (numLoad of them) of a TM2 list -> this AC's VRAM page. Native for the
 * per-AC page (TBP from acid); sic, a zero entry loops forever as in the original. */
static void __LoadOtherPartsTex(int idxStart, int numLoad, int acid, char *pbufTex)
{
    PS2_FRAME(0x80);
    int tbp = ac_tbp(acid), cbp = ac_cbp(acid);
    int *ent = (int *)(pbufTex + idxStart * 0x10 + 0x10);
    for (int n = 0; (unsigned int)idxStart < *(unsigned int *)(pbufTex + 4) && n < numLoad;) {
        if (*ent) {
            DF_ReadTM2Pointer_TSS(pbufTex + *ent, 1, tbp, cbp);
            ent += 4;
            idxStart++;
            n++;
        }
    }
}

/* ... body parts: the TM2's CLUT is first recoloured with the player's 5 part colours (alpha 0x80, built at the
 * original stack slot sp + 0x90) */
static void __LoadBodyPartsTex(unsigned char (*clutcol)[3], int idxStart, int numLoad, int acid, char *pbufTex)
{
    PS2_FRAME(0xb0);
    PS2_STACK(StColorU8[5], col, 0x90);
    unsigned char *c = (unsigned char *)col;
    for (int i = 0; i < 5; i++) { c[i * 4] = clutcol[i][0]; c[i * 4 + 1] = clutcol[i][1]; c[i * 4 + 2] = clutcol[i][2]; c[i * 4 + 3] = 0x80; }
    int tbp = ac_tbp(acid), cbp = ac_cbp(acid);
    int *ent = (int *)(pbufTex + idxStart * 0x10 + 0x10);
    for (int n = 0; (unsigned int)idxStart < *(unsigned int *)(pbufTex + 4) && n < numLoad;) {
        if (*ent) {
            char *bufp = pbufTex + *ent;
            GAcUtil_MakeBodyPartsClut((unsigned int *)(bufp + *(int *)(bufp + 4) + 0x20), col);
            DF_ReadTM2Pointer_TSS(bufp, 1, tbp, cbp);
            ent += 4;
            idxStart++;
            n++;
        }
    }
}

/* Model attributes come in LOD pairs: ...4/8/0xc..0x20 = low-detail model, 3/7/0xb..0x1f = normal model. */
static inline int ld_want_lod(StAcLoader *t, unsigned char a)
{
    if (a == 0x20 || a == 0x1c || a == 0x18 || a == 0x14 || a == 0x10 || a == 0xc || a == 8 || a == 4)
        return t->m_isLow == 1;
    if (a == 0x1f || a == 0x1b || a == 0x17 || a == 0x13 || a == 0xf || a == 0xb || a == 7 || a == 3)
        return t->m_isLow == 0;
    return 0;
}

static inline void ld_model(StAcLoader *t, StModel *mp, unsigned int anim)
{
    char *cur = t->m_pbufCur;
    CopyMemory8b(cur, t->m_pbufData, t->m_sizeData);
    t->m_pbufCur = align128(DF_ReadOMDPointer(cur, mp, anim));
}

static inline void ld_lod_model(StAcLoader *t, StModel *mp, unsigned int anim)
{
    if (ld_want_lod(t, t->m_atrbData)) ld_model(t, mp, anim);
}

/* Shadow model (only with AcOpt.shadow): its own object list sized like the real model. */
static inline void ld_shadow(StAcLoader *t, StModel *ms, StModel *mp, unsigned int anim)
{
    char *cur = t->m_pbufCur;
    if (AcOpt.shadow) {
        CopyMemory8b(cur, t->m_pbufData, t->m_sizeData);
        void *end = DF_ReadOMDPointer(cur, ms, anim);
        ms->op = MDL_AllocObject(mp->nobj);
        cur = align128(end);
    }
    t->m_pbufCur = cur;
}

static inline void ld_wsfx(StAcLoader *t)
{
    unsigned int size = t->m_sizeData;
    char *cur = t->m_pbufCur;
    StLoadParts *lp = t->m_plp;
    AcPtsResSt *pres = t->m_ppres;
    CopyMemory8b(cur, t->m_pbufData, size);
    AcPtsRes_RegisterWsfx(pres, lp->m_psr, cur);
    t->m_pbufCur = align128(cur + size);
}

static inline void ld_param(StAcLoader *t) { CopyMemory8b(t->m_plp->m_pParam, t->m_pbufData, t->m_plp->m_sizeParam); }

static inline void ld_anim(StAcLoader *t, StModel *mp, int isWep, int disc)
{
    t->m_pbufCur = __BringAnim(mp, t->m_idAc, t->m_pbufCur, t->m_pbufData, t->m_sizeData, isWep, disc, 0);
}

static inline void ld_addwep(StAcLoader *t, StModel *mp) { t->m_pmWep[t->m_cntWep++] = mp; }

/* AC body model (and its shadow): the first body file starts the OMD extension memory, the others add to it. */
static inline void ld_body_model(StAcLoader *t, int first)
{
    int acid = t->m_idAc;
    CopyMemory8b(t->m_pbufCur, t->m_pbufData, t->m_sizeData);
    char *end;
    if (first) {
        t->m_omdmemI.omdtable = AcOMDTable[acid];
        t->m_omdmemI.otp = ACTP2ObjectTable[t->m_idAc];
        t->m_omdmemI.vu1mp = (unsigned int *)(t->m_pbufCur + tp2_GetOmdVu1mpOffset(t->m_pbufCur));
        end = tp2_ReadOmdPointer_ExtMemFirst(t->m_pbufCur, &MyAC[acid], 1, &t->m_omdmemI);
    } else {
        end = tp2_ReadOmdPointer_ExtMemAdd(t->m_pbufCur, &MyAC[acid], &t->m_omdmemI);
    }
    t->m_pbufCur = align128(end);
}

static inline void ld_body_shadow(StAcLoader *t, int first)
{
    char *cur;
    if (!AcOpt.shadow) {
        cur = t->m_pbufCur;
    } else {
        CopyMemory8b(t->m_pbufCur, t->m_pbufData, t->m_sizeData);
        int acid = t->m_idAc;
        if (first) {
            t->m_omdmemS.omdtable = AcOMDTableS[acid];
            t->m_omdmemS.otp = ACTP2ObjectTableS[t->m_idAc];
            t->m_omdmemS.vu1mp = (unsigned int *)(t->m_pbufCur + tp2_GetOmdVu1mpOffset(t->m_pbufCur));
            cur = tp2_ReadOmdPointer_ExtMemFirst(t->m_pbufCur, &MyACS[acid], 1, &t->m_omdmemS);
        } else {
            cur = tp2_ReadOmdPointer_ExtMemAdd(t->m_pbufCur, &MyACS[t->m_idAc], &t->m_omdmemS);
        }
        cur = align128(cur);
    }
    t->m_pbufCur = cur;
}

/* Records every body file has: body model (pass 0), body shadow (pass 1), weapon sfx (pass 2), params (pass 0). */
static inline int ld_body_common(StAcLoader *t, int pass, int first)
{
    unsigned char a = t->m_atrbData;
    if (a == 5) {
        if (pass == 1) ld_body_shadow(t, first);
    } else if (a == 4 || a == 3) {
        if (pass == 0 && ld_want_lod(t, a)) ld_body_model(t, first);
    } else if (a == 2) {
        if (pass == 2) ld_wsfx(t);
    } else if (a == 1) {
        if (pass == 0) ld_param(t);
    } else {
        return 0;
    }
    return 1;
}

static inline void ld_monoeye(StAcLoader *t, int tm2)
{
    CopyMemory8b(t->m_pbufCur, t->m_pbufData, t->m_sizeData);
    if (tm2) AcPtsRes_RegisterMonoeyeTm2(t->m_ppres, t->m_pbufCur);
    else AcPtsRes_RegisterMonoeyeTp2(t->m_ppres, t->m_pbufCur);
    t->m_pbufCur = align128(t->m_pbufCur + t->m_sizeData);
}

/* Selects table entry ptsid as the current part and reads its file id (idParts + idCdvd base). */
static inline int ld_select(StAcLoader *t, int ptsid)
{
    t->m_plp = &t->m_tblParts[ptsid];
    t->m_kind = t->m_plp->m_kind;
    t->m_pfi = aci_GetPartsFileAtrbInfo(t->m_kind);
    return (int)(t->m_plp->m_idCdvd + t->m_plp->m_idParts);
}

/* Unpacks the parts file in pbuf (LZSS after a 0x20 header) into g_pbufRead and points m_pdhead / m_pphead at it. */
static inline void ld_unpack_header(StAcLoader *t, char *pbuf)
{
    FsLZSSUncompress((unsigned char *)g_pbufRead, (unsigned char *)pbuf + 0x20);
    t->m_pdhead = (StDownloadPackHeader *)g_pbufRead;
    t->m_pphead = (StPartsDataHeader *)(g_pbufRead + t->m_pdhead->uiOffset[0]);
}

/* Reads parts file ptsid into pbuf (sizeBuf -1 = no limit) and unpacks its header; 0 if it does not fit. */
static int __CatchFile(StAcLoader *this_, int ptsid, char *pbuf, int sizeBuf)
{
    PS2_FRAME(0x50);
    int cdvdid = ld_select(this_, ptsid);
    int sizeNeed = File_GetSize(cdvdid);
    if (sizeBuf != -1 && sizeBuf < sizeNeed) return 0;
    File_NlRead(cdvdid, -1, pbuf);
    FsLZSSGetUncompressSize((_FS_LZSS_FILE_HEADER_ST *)(pbuf + 0x20)); /* sic, result unused, asked again below */
    if (FsLZSSGetUncompressSize((_FS_LZSS_FILE_HEADER_ST *)(pbuf + 0x20)) >= 0x100001) return 0;
    ld_unpack_header(this_, pbuf);
    return 1;
}

/* Body parts file ptsid: isLoad -> read it into pbuf and return the 16-aligned end; else unpack the header of the file
 * already in pbuf and return g_pbufRead. */
static char *__CatchBodyFile(StAcLoader *this_, int ptsid, char *pbuf, int sizeBuf, int isLoad)
{
    PS2_FRAME(0x50);
    this_->m_plp = &this_->m_tblParts[ptsid];
    this_->m_kind = this_->m_plp->m_kind;
    this_->m_pfi = aci_GetPartsFileAtrbInfo(this_->m_kind);
    if (isLoad) {
        int cdvdid = (int)(this_->m_plp->m_idCdvd + this_->m_plp->m_idParts);
        int sizeNeed = File_GetSize(cdvdid);
        if (sizeBuf != -1 && sizeBuf < sizeNeed) return 0;
        File_NlRead(cdvdid, -1, pbuf);
        return (char *)(((unsigned int)pbuf + File_GetRealSize(cdvdid) + 0xf) & 0xfffffff0u);
    }
    if (FsLZSSGetUncompressSize((_FS_LZSS_FILE_HEADER_ST *)(pbuf + 0x20)) >= 0x100001) return 0;
    ld_unpack_header(this_, pbuf);
    return g_pbufRead;
}

/* Copies an anim block to pbufCur, reads it into pm and sets the model flags (weapon: type 7, disc in `on`, mirrored
 * X scale for isMirror; body: type 4). Returns the 128-aligned end of the anim. */
static char *__BringAnim(StModel *pm, int acid, char *pbufCur, char *pbufData, unsigned int sizeData, int isWep, int disc,
                         int isMirror)
{
    PS2_FRAME(0x70);
    CopyMemory8b(pbufCur, pbufData, (int)sizeData);
    char *pbufNext = Model_ReadAnim(pm, pbufCur, 1);
    if (isWep) {
        pm->type = 7;
        pm->vuparam.mulalpha = 1.0f;
        pm->aid = (unsigned short)acid;
        pm->on = (unsigned short)disc;
        pm->blend2 = 0x68;
        pm->alpha2 = 0x40;
        pm->on_layer = 0xff;
        pm->scale[0] = isMirror ? -1.0f : 1.0f;
        pm->scale[1] = 1.0f;
        pm->scale[2] = 1.0f;
        pm->scale[3] = 1.0f;
    } else {
        pm->type = 4;
        pm->vuparam.mulalpha = 1.0f;
        pm->aid = (unsigned short)acid;
        pm->blend2 = 0x68;
        pm->alpha2 = 0x40;
        pm->on_layer = 0xff;
        pm->scale[0] = 1.0f;
        pm->scale[1] = 1.0f;
        pm->scale[2] = 1.0f;
        pm->scale[3] = 1.0f;
    }
    return align128(pbufNext);
}

static int __LoadAcPartsBody(StAcLoader *this_, char **ppbufBsAnim, unsigned int *psizeBsAnim)
{
    PS2_FRAME(0xb0);
    AcAssemblyPartsSt *asm_;
    char *p = __CatchBodyFile(this_, 1, this_->m_pbufBody[0], -1, 1);
    if (!p) return 0;
    this_->m_pbufBody[1] = p;
    if (!(p = __CatchBodyFile(this_, 0, p, -1, 1))) return 0;
    this_->m_pbufBody[2] = p;
    if (!(p = __CatchBodyFile(this_, 2, p, -1, 1))) return 0;
    this_->m_pbufBody[3] = p;
    if (!__CatchBodyFile(this_, 3, p, -1, 1)) return 0;

    for (int pass = 0; pass < 3; pass++) {
        /* core (ptsid 1): inside model / anim, booster anim */
        if (this_->m_pbufBody[0] <= this_->m_pbufCur) return 0;
        if (!__CatchBodyFile(this_, 1, this_->m_pbufBody[0], -1, 0)) return 0;
        for (int i = 0; i < 24; i++) {
            ld_entry(this_, i);
            unsigned char a = this_->m_atrbData;
            if (a == 0x26) continue;
            int acid = this_->m_idAc;
            if (a == 0x25) {
                asm_ = this_->m_pasmbly;
                __LoadBodyPartsTex(&asm_->color[1].color.rgb[0], asm_->color[1].camouflage, 1, this_->m_idAc,
                                   this_->m_pbufData);
            } else if (a == 0x16 || a == 0x12) {
                if (pass == 2) ld_anim(this_, &MyEOB[acid], 0, 0);
            } else if (a == 0x15 || a == 0x11) {
                if (pass == 2) ld_shadow(this_, &MyEOBS[acid], &MyEOB[acid], 1);
            } else if (a == 0x14 || a == 0x13 || a == 0x10 || a == 0xf) {
                if (pass == 2) ld_lod_model(this_, &MyEOB[acid], 1);
            } else if (a == 6) {
                if (pass == 2) {
                    *psizeBsAnim = this_->m_sizeData;
                    *ppbufBsAnim = this_->m_pbufCur;
                    CopyMemory8b(*ppbufBsAnim, this_->m_pbufData, *psizeBsAnim);
                    this_->m_pbufCur = align128(this_->m_pbufCur + *psizeBsAnim);
                }
            } else {
                ld_body_common(this_, pass, 1);
            }
        }
        /* head (ptsid 0): mono-eye */
        if (this_->m_pbufBody[1] <= this_->m_pbufCur) return 0;
        if (!__CatchBodyFile(this_, 0, this_->m_pbufBody[1], -1, 0)) return 0;
        for (int i = 0; i < 24; i++) {
            ld_entry(this_, i);
            unsigned char a = this_->m_atrbData;
            if (a == 0x26) continue;
            if (a == 0x25) {
                if (pass == 2) {
                    asm_ = this_->m_pasmbly;
                    __LoadBodyPartsTex(&asm_->color[0].color.rgb[0], asm_->color[0].camouflage, 1, this_->m_idAc,
                                       this_->m_pbufData);
                }
            } else if (a == 0x24) {
                if (pass == 2) ld_monoeye(this_, 1);
            } else if (a == 0x23) {
                if (pass == 2) ld_monoeye(this_, 0);
            } else {
                ld_body_common(this_, pass, 0);
            }
        }
        /* arms (ptsid 2): arm weapons, arm covers */
        if (this_->m_pbufBody[2] <= this_->m_pbufCur) return 0;
        if (!__CatchBodyFile(this_, 2, this_->m_pbufBody[2], -1, 0)) return 0;
        for (int i = 0; i < 24; i++) {
            ld_entry(this_, i);
            unsigned char a = this_->m_atrbData;
            if (a == 0x26) continue;
            int acid = this_->m_idAc;
            StModel *lhw = &MyLHW[acid], *rhw = &MyRHW[acid];
            if (a == 0x25) {
                if (pass == 2) {
                    asm_ = this_->m_pasmbly;
                    __LoadBodyPartsTex(&asm_->color[3].color.rgb[0], asm_->color[3].camouflage << 2, 2, this_->m_idAc,
                                       this_->m_pbufData);
                    asm_ = this_->m_pasmbly;
                    __LoadBodyPartsTex(&asm_->color[2].color.rgb[0], asm_->color[2].camouflage * 4 + 2, 2,
                                       this_->m_idAc, this_->m_pbufData);
                }
            } else if (a == 0x1e) {
                if (pass == 2) {
                    ld_anim(this_, lhw, 1, this_->m_plp->m_idParts + 0x400);
                    ld_addwep(this_, &MyLHW[this_->m_idAc]);
                }
            } else if (a == 0x1d) {
                if (pass == 2) ld_shadow(this_, &MyLHWS[acid], lhw, 1);
            } else if (a == 0x1c || a == 0x1b) {
                if (pass == 2) ld_lod_model(this_, lhw, 1);
            } else if (a == 0x1a) {
                if (pass == 2) {
                    ld_anim(this_, rhw, 1, this_->m_plp->m_idParts + 0x400);
                    ld_addwep(this_, &MyRHW[this_->m_idAc]);
                }
            } else if (a == 0x19) {
                if (pass == 2) ld_shadow(this_, &MyRHWS[acid], rhw, 1);
            } else if (a == 0x18 || a == 0x17) {
                if (pass == 2) ld_lod_model(this_, rhw, 1);
            } else if (a == 0xe) {
                if (pass == 2) ld_anim(this_, &MyLCV[acid], 0, this_->m_plp->m_idParts + 0x2000);
            } else if (a == 0xc || a == 0xb) {
                if (pass == 2) ld_lod_model(this_, &MyLCV[acid], 0);
            } else if (a == 0xa) {
                if (pass == 2) ld_anim(this_, &MyRCV[acid], 0, this_->m_plp->m_idParts + 0x1c00);
            } else if (a == 8 || a == 7) {
                if (pass == 2) ld_lod_model(this_, &MyRCV[acid], 0);
            } else {
                ld_body_common(this_, pass, 0);
            }
        }
        /* legs (ptsid 3) */
        if (this_->m_pbufBody[3] <= this_->m_pbufCur) return 0;
        if (!__CatchBodyFile(this_, 3, this_->m_pbufBody[3], -1, 0)) return 0;
        for (int i = 0; i < 24; i++) {
            ld_entry(this_, i);
            unsigned char a = this_->m_atrbData;
            if (a == 0x26) continue;
            int acid = this_->m_idAc;
            if (a == 0x25) {
                if (pass == 2) {
                    asm_ = this_->m_pasmbly;
                    __LoadBodyPartsTex(&asm_->color[4].color.rgb[0], asm_->color[4].camouflage << 1, 2, this_->m_idAc,
                                       this_->m_pbufData);
                }
            } else if (a == 0x22) {
                if (pass == 2) ld_anim(this_, &MySLG[acid], 0, 0);
            } else if (a == 0x21) {
                if (pass == 2) ld_shadow(this_, &MySLGS[acid], &MySLG[acid], 1);
            } else if (a == 0x20 || a == 0x1f) {
                if (pass == 2) ld_lod_model(this_, &MySLG[acid], 1);
            } else {
                ld_body_common(this_, pass, 0);
            }
        }
        if (pass == 1) MyACS[this_->m_idAc].op = MDL_AllocObject(MyAC[this_->m_idAc].nobj);
        if (pass == 0) {
            /* body animation set chosen by core / arms / legs type */
            int idFile = (short)AcLoader_GetAnimIdFromCoreArmLeg(this_->m_ppartsd) + 60000;
            File_NlRead(idFile, 0x100000, g_pbufRead);
            int acid = this_->m_idAc;
            unsigned int size = File_GetRealSize(idFile);
            this_->m_pbufCur = __BringAnim(&MyAC[acid], acid, this_->m_pbufCur, g_pbufRead, size, 0, 0, 0);
            MyAC[this_->m_idAc].disp_ap = this_->m_type != 0;
        }
    }
    unsigned char legs = this_->m_ppartsd->legs.type;
    if (this_->m_ppartsd->arms.type != 0) {   /* weapon arms: no arm (hand) weapons */
        this_->m_pasmbly->idParts[12] = 0;
        this_->m_tblParts[12].m_idParts = 0;
        this_->m_pasmbly->idParts[13] = 0;
        this_->m_tblParts[13].m_idParts = 0;
    }
    if (legs == 3 || legs == 4) {
        this_->m_pasmbly->idParts[4] = 1;
        this_->m_tblParts[4].m_idParts = 1;
    }
    return 1;
}

static inline int ld_catch(StAcLoader *t, int ptsid)
{
    return __CatchFile(t, ptsid, t->m_pbufCur, s_sizeAcBuff[t->m_idAc] - (t->m_pbufCur - t->m_pbufTop));
}

static inline unsigned int part_color(StAcLoader *t, int k) { return t->m_pasmbly->color[k].color.rgb[0][0]; }

/* One weapon file: tex (color index colorTex), anim (attribute an), shadow (an - 1), model (an - 2, an - 3). */
static int ld_weapon(StAcLoader *t, int ptsid, unsigned int colorTex, unsigned char an, StModel *mp, StModel *ms,
                     int disc, int addwep)
{
    if (!ld_catch(t, ptsid)) return 0;
    for (int i = 0; i < 24; i++) {
        ld_entry(t, i);
        unsigned char a = t->m_atrbData;
        if (a == 0x26) continue;
        if (a == 0x25) __LoadOtherPartsTex(colorTex, 1, t->m_idAc, t->m_pbufData);
        else if (a == an) {
            ld_anim(t, mp, 1, t->m_plp->m_idParts + disc);
            if (addwep) ld_addwep(t, mp);
        } else if (a == an - 1) ld_shadow(t, ms, mp, 1);
        else if (a == an - 2 || a == an - 3) ld_lod_model(t, mp, 1);
        else if (a == 2) ld_wsfx(t);
        else if (a == 1) ld_param(t);
    }
    return 1;
}

static int __LoadAcPartsOther(StAcLoader *this_, char *pbufBsAnim, unsigned int sizeBsAnim)
{
    PS2_FRAME(0xb0);
    int acid = this_->m_idAc;
    /* booster */
    if (!ld_catch(this_, 4)) return 0;
    for (int i = 0; i < 24; i++) {
        ld_entry(this_, i);
        unsigned char a = this_->m_atrbData;
        if (a == 0x26) continue;
        if (a == 0x25) __LoadOtherPartsTex(part_color(this_, 5), 1, this_->m_idAc, this_->m_pbufData);
        else if (a == 5) ld_shadow(this_, &MyBSTS[acid], &MyBST[acid], 1);
        else if (a == 4 || a == 3) ld_lod_model(this_, &MyBST[acid], 1);
        else if (a == 2) ld_wsfx(this_);
        else if (a == 1) ld_param(this_);
    }
    __BringAnim(&MyBST[acid], this_->m_idAc, pbufBsAnim, pbufBsAnim, sizeBsAnim, 0, 0, 0);

    /* shoulder weapons (core addon bit 2, no weapon arms) */
    AcAssemblyPartsSt *pa = this_->m_pasmbly;
    if (pa->idParts[14] < 1 || this_->m_ppartsd->arms.type != 0 || !(this_->m_ppartsd->core.addon & 4)) {
        this_->m_pasmbly->idParts[14] = 0;
    } else {
        acid = this_->m_idAc;
        if (!ld_weapon(this_, 14, part_color(this_, 11), 0xa, &MyRSW[acid], &MyRSWS[acid],
                       0x400, 0))
            return 0;
    }
    pa = this_->m_pasmbly;
    if (pa->idParts[15] < 1 || this_->m_ppartsd->arms.type != 0 || !(this_->m_ppartsd->core.addon & 4)) {
        this_->m_pasmbly->idParts[15] = 0;
    } else {
        acid = this_->m_idAc;
        if (!ld_weapon(this_, 15, part_color(this_, 12), 0xe, &MyLSW[acid], &MyLSWS[acid],
                       0x800, 0))
            return 0;
    }
    if (this_->m_pasmbly->idParts[14] > 0 || this_->m_pasmbly->idParts[15] > 0) {
        /* shoulder weapon pages: pull the AC's texture + CLUT back from VRAM and keep copies */
        acid = this_->m_idAc;
        StAcTextureBuffer *tex = g_pactex[acid];
        int tbp = 0, cbp = 0;
        if (AcOpt.txx == 0) {
            tbp = acid << 10;
            cbp = acid << 5;
        }
        DF_StoreImage(tex, tbp + 0x1500, 0, 0, 0, 0x40, 0x400);
        DF_StoreImage(&tex->clutbuf, cbp + 0x3d40, 0, 0, 0, 0x40, 0x20);
        CopyMemory(tex->texbufSpwR, (char *)&tex->texbuf + 0x3a000, 0x2000);
        __CopyPageClutPsmt4((unsigned char *)tex->clutbufSpwR, (unsigned char *)&tex->clutbuf, 0x38, 8, 1);
        CopyMemory(tex->texbufSpwL, (char *)&tex->texbuf + 0x3c000, 0x2000);
        __CopyPageClutPsmt4((unsigned char *)tex->clutbufSpwL, (unsigned char *)&tex->clutbuf, 0x20, 0x10, 1);
    }
    /* hand weapons */
    if (this_->m_ppartsd->arms.type == 0 && this_->m_pasmbly->idParts[12] > 0) {
        acid = this_->m_idAc;
        StModel *mp = &MyRHW[acid];
        if (!ld_weapon(this_, 12, part_color(this_, 9), 0xa, mp, &MyRHWS[acid], 0x400, 1)) return 0;
        g_pmodelArmWepR[this_->m_idAc] = mp;
    }
    if (this_->m_ppartsd->arms.type == 0 && this_->m_pasmbly->idParts[13] > 0) {
        acid = this_->m_idAc;
        StModel *mp = &MyLHW[acid];
        if (!ld_weapon(this_, 13, part_color(this_, 10), 0xe, mp, &MyLHWS[acid], 0x800, 1)) return 0;
        g_pmodelArmWepL[this_->m_idAc] = mp;
    }
    /* back weapons; a two-slot back weapon (backR.w_use) also fills the left slot */
    if (this_->m_pasmbly->idParts[10] > 0) {
        acid = this_->m_idAc;
        if (!ld_weapon(this_, 10, part_color(this_, 7) * 2 + 1, 0xa, &MyRBW[acid],
                       &MyRBWS[acid], 0xc00, 1))
            return 0;
        if (this_->m_ppartsd->backR.w_use) {
            short id = this_->m_pasmbly->idParts[10];
            this_->m_pasmbly->idParts[11] = id;
            this_->m_tblParts[11].m_idParts = id;
            memcpy(&this_->m_ppartsd->backL, &this_->m_ppartsd->backR, sizeof(PartsParamBackSt));
        }
    }
    if (this_->m_pasmbly->idParts[11] > 0) {
        acid = this_->m_idAc;
        StModel *mp = &MyLBW[acid], *ms = &MyLBWS[acid];
        if (!ld_catch(this_, 11)) return 0;
        for (int i = 0; i < 24; i++) {
            ld_entry(this_, i);
            unsigned char a = this_->m_atrbData;
            if (a == 0x26 || a == 0xa || a == 9 || a == 8 || a == 7) continue;
            if (a == 0x25) {
                unsigned int col = this_->m_ppartsd->backL.w_use ? part_color(this_, 7) : part_color(this_, 8);
                __LoadOtherPartsTex(col << 1, 1, this_->m_idAc, this_->m_pbufData);
            } else if (a == 0xe) {
                if (mp && ms) {
                    ld_anim(this_, mp, 1, this_->m_plp->m_idParts + 0x1000);
                    ld_addwep(this_, mp);
                }
            } else if (a == 0xd) {
                if (ms) ld_shadow(this_, ms, mp, 1);
            } else if (a == 0xc || a == 0xb) {
                if (mp) ld_lod_model(this_, mp, 1);
            } else if (a == 2) {
                ld_wsfx(this_);
            } else if (a == 1) {
                ld_param(this_);
                if (!this_->m_ppartsd->backR.w_use && this_->m_ppartsd->backL.w_use) {
                    /* a two-slot weapon in the left slot alone is dropped */
                    this_->m_pasmbly->idParts[11] = 0;
                    ZeroMemory8b(mp, 0x180);
                    ZeroMemory8b(ms, 0x180);
                    mp = ms = 0;
                }
            }
        }
    }
    /* extensions (one file for both sides) */
    if (this_->m_pasmbly->idParts[9] > 0) {
        if (!ld_catch(this_, 9)) return 0;
        for (int i = 0; i < 24; i++) {
            ld_entry(this_, i);
            unsigned char a = this_->m_atrbData;
            if (a == 0x26) continue;
            acid = this_->m_idAc;
            if (a == 0x25) {
                __LoadOtherPartsTex(0, 1, this_->m_idAc, this_->m_pbufData);
            } else if (a == 0xe) {
                ld_anim(this_, &MyLEX[acid], 1, this_->m_plp->m_idParts + 0x1800);
                ld_addwep(this_, &MyLEX[this_->m_idAc]);
            } else if (a == 0xd) {
                ld_shadow(this_, &MyLEXS[acid], &MyLEX[acid], 0);
            } else if (a == 0xc || a == 0xb) {
                ld_lod_model(this_, &MyLEX[acid], 0);
            } else if (a == 0xa) {
                ld_anim(this_, &MyREX[acid], 1, this_->m_plp->m_idParts + 0x1400);
                ld_addwep(this_, &MyREX[this_->m_idAc]);
            } else if (a == 9) {
                ld_shadow(this_, &MyREXS[acid], &MyREX[acid], 0);
            } else if (a == 8 || a == 7) {
                ld_lod_model(this_, &MyREX[acid], 0);
            } else if (a == 2) {
                ld_wsfx(this_);
            } else if (a == 1) {
                ld_param(this_);
            }
        }
    }
    /* inside parts: params + sfx only */
    if (this_->m_pasmbly->idParts[8] > 0) {
        if (!ld_catch(this_, 8)) return 0;
        for (int i = 0; i < 24; i++) {
            ld_entry(this_, i);
            unsigned char a = this_->m_atrbData;
            if (a == 2) ld_wsfx(this_);
            else if (a == 1) ld_param(this_);
        }
    }
    return 1;
}

/* Model-less parts (table entries 5-7: FCS, generator, radiator): file -> g_pbufRead -> the parts param struct. */
static int __LoadAcPartsNoModel(StAcLoader *this_)
{
    PS2_FRAME(0x30);
    for (int k = 5; k < 8; k++) {
        this_->m_plp = &this_->m_tblParts[k];
        this_->m_kind = this_->m_plp->m_kind;
        int cdvdid = (int)(this_->m_plp->m_idCdvd + this_->m_plp->m_idParts);
        if (File_GetSize(cdvdid) >= 0x100001) return 0;
        File_NlRead(cdvdid, -1, g_pbufRead);
        CopyMemory(this_->m_plp->m_pParam, g_pbufRead, this_->m_plp->m_sizeParam);
    }
    return 1;
}

static int __LoadAcParts(StAcLoader *this_)
{
    PS2_FRAME(0x30);
    char *pbufBsAnim = 0;
    unsigned int sizeBsAnim = 0;
    if (!__LoadAcPartsNoModel(this_)) return 0;
    if (!__LoadAcPartsBody(this_, &pbufBsAnim, &sizeBsAnim)) return 0;
    return __LoadAcPartsOther(this_, pbufBsAnim, sizeBsAnim) != 0;
}

static void __SetupAcTexture(int acid)
{
    PS2_FRAME(0x60);
    StAcTextureBuffer *tex = g_pactex[acid];
    int tbp = 0, cbp = 0;
    if (AcOpt.txx == 0) {
        tbp = acid << 10;
        cbp = acid << 5;
    }
    /* read back what the parts loaders drew into VRAM, then build the re-upload lists (whole page, 7 sub-pages) */
    DF_StoreImage(tex, tbp + 0x1500, 0, 0, 0, 0x40, 0x400);
    DF_StoreImage(&tex->clutbuf, cbp + 0x3d40, 0, 0, 0, 0x40, 0x20);
    static const struct { short tag, off, tbp, ht; } up[8] = {
        {0, 0, 0x1500, 0x340},
        {9, 0x3e0, 0x18e0, 0x20}, {1, 0x3a0, 0x18a0, 0x20}, {2, 0x3c0, 0x18c0, 0x20}, {3, 0x360, 0x1860, 0x20},
        {4, 0x380, 0x1880, 0x20}, {5, 0x340, 0x1840, 0x20}, {6, 0x340, 0x1840, 0x20},
    };
    for (int k = 0; k < 8; k++) {
        unsigned int *gp = (unsigned int *)tex->dmatag[up[k].tag];
        GL_LoadImage(&gp, (char *)tex + up[k].off * 0x100, tbp + up[k].tbp, 0x40, up[k].ht);
        GL_EndList(&gp);
    }
}

static int __SetupAcModel(StAcLoader *this_)
{
    PS2_FRAME(0x70);
    unsigned int callerS2 = PS2_STALE_GPR(S2);   /* see the m_type == 1 case below */
    AcPartsDataSt *pd = this_->m_ppartsd;
    int acid = this_->m_idAc;
    unsigned char legs = pd->legs.type;
    if (legs == 3 || legs == 4) {
        MyBST[acid].op[1].onoff = 1;
        MyBST[acid].op[2].onoff = 1;
    }
    /* arms objdisp bits hide arm objects (pairs 5/9, 6/10, 7/11, 8/12) */
    StModel *body = &MyAC[acid];
    for (int b = 0; b < 4; b++) {
        body->op[5 + b].onoff = !(pd->arms.objdisp & (1 << b));
        body->op[9 + b].onoff = !(pd->arms.objdisp & (1 << b));
    }
    int tbp = ac_tbp(this_->m_idAc), cbp = ac_cbp(this_->m_idAc);
    StModel *tbl[6] = {&MyAC[this_->m_idAc], &MyLCV[this_->m_idAc], &MyRCV[this_->m_idAc],
                       &MyBST[this_->m_idAc], &MyEOB[this_->m_idAc],
                       &MySLG[this_->m_idAc]};
    for (int k = 0; k < 6; k++) DF_CALC_MyACTSS(tbl[k], tbp, cbp, tbl[k]->omdtable);
    StModel *ins = &MyINS[this_->m_idAc];
    DF_CALC_MyACTSS(ins, 0, cbp, ins->omdtable);
    for (int i = 0; i < this_->m_cntWep; i++) DF_CALC_MyACTSS(this_->m_pmWep[i], tbp, cbp, this_->m_pmWep[i]->omdtable);
    StModel *sw = &MyRSW[this_->m_idAc];
    if (sw->nobj) DF_CALC_MyACTSS(sw, tbp, cbp, sw->omdtable);
    sw = &MyLSW[this_->m_idAc];
    if (sw->nobj) DF_CALC_MyACTSS(sw, tbp, cbp, sw->omdtable);
    NL_LoopNowLoading(1);
    if (this_->m_idMac < 0) {
        acid = this_->m_idAc;
        if (g_sortie.pconstant->modeSortie != 0 || acid == MyI.myplayerno[0])
            DF_ReadTM2Pointer_TSS(EmblemSTM2[acid], 1, ac_tbp(acid), ac_cbp(acid));
    }
    char *bufp = g_pbufRead;
    if (this_->m_idMac < 0) {
        __SetupAcTexture(this_->m_idAc);
    } else {
        /* AC texture file: m_type 1 leaves the file id to whatever was in the caller's s2 (original code path,
         * not reachable from versus: LoopSortie passes macid -1) */
        int idFile = (int)callerS2;
        if (this_->m_type != 1) idFile = this_->m_idMac + 0x1130;
        tbp = ac_tbp(this_->m_idAc);
        cbp = ac_cbp(this_->m_idAc);
        if (g_sortie.pconstant->modeSortie == 0 || this_->m_idAc != MyI.myplayerno[0]) idFile = 0x1130;
        File_NlRead(idFile, -1, g_pbufRead);
        DF_ReadTMXPointer_TSS(bufp, 1, tbp, cbp);
        __SetupAcTexture(this_->m_idAc);
    }
    for (int i = 0; i < 13; i++) {
        WepIconFileHdr *hdr = (WepIconFileHdr *)AcPtsRes_GetWeaponIcon(this_->m_ppres, i);
        if (hdr) WEPICON_CreateIcon(hdr);
    }
    return 1;
}

static void __InitAcModel(int acid)
{
    PS2_FRAME(0xb0);
    StModel *tbl[25] = {
        &MyAC[acid],                 &MyACS[acid],  &MyRCV[acid],
        &MyRCVS[acid],    &MyLCV[acid],  &MyLCVS[acid],
        &MyBST[acid],     &MyBSTS[acid], &MyEOB[acid],
        &MyEOBS[acid],    &MySLG[acid],  &MySLGS[acid],
        &MyRHW[acid],     &MyRHWS[acid], &MyLHW[acid],
        &MyLHWS[acid],    &MyRBW[acid],  &MyRBWS[acid],
        &MyLBW[acid],     &MyLBWS[acid], &MyREX[acid],
        &MyREXS[acid],    &MyLEX[acid],  &MyLEXS[acid],
        &MyINS[acid],
    };
    for (int k = 0; k < 25; k++) ZeroMemory8b(tbl[k], 0x180);
    g_pmodelArmWepR[acid] = 0;
    g_pmodelArmWepL[acid] = 0;
}

char *AcLoader_LoadAndSetup(char *pbuf, int acid, int macid, int low)
{
    PS2_FRAME(0x280);
    /* the loader lives at the original stack slot: interpreted helpers keep pointers into it during the load */
    PS2_STACK(StAcLoader, loader, 0xa0);
    StAcLoader *ld = &loader;
    AcAssemblyPartsSt *pasmbly = Sortie_GetAcAssemblyForInitializer(acid);
    AcPlayerSt *acp = &g_ac[acid];
    AcPartsDataSt *pd = AcInst_GetAllPartsParam(acp);
    char *body = (char *)Memm_GetSpecialReadBuffer(0xc8000);
    ZeroMemory(pd, 0x2470);
    ZeroMemory(ld, 0x1dc);
    ld->m_isLow = low == 1;
    ld->m_idAc = acid;
    ld->m_idMac = macid;
    ld->m_pac = acp;
    ld->m_pasmbly = pasmbly;
    ld->m_ppartsd = pd;
    ld->m_ppres = AcCtrl_GetPartsRes(acp);
    if (ld->m_idMac >= 0) {
        ld->m_type = ld->m_idMac >> 16 & 0xffff;
        ld->m_idMac &= 0xffff;
    }
    ld->m_pbufCur = pbuf;
    ld->m_pbufTop = pbuf;
    ld->m_pbufBody[0] = body;
    __SetupLoadPartsInfo(ld);
    if (!__LoadAcParts(ld)) {
        Memm_ReleaseSpecialReadBuffer();
        ld->m_pbufCur = 0;
    } else {
        Memm_ReleaseSpecialReadBuffer();
        if (!__SetupAcModel(ld)) ld->m_pbufCur = 0;
    }
    return ld->m_pbufCur;
}

int AcLoader_AllocAcBuffer(void)
{
    PS2_FRAME(0x50);
    int i;
    for (i = 0; i < NowPlayerNum; i++) {
        g_pbufAc[i] = 0;
        s_sizeAcBuff[i] = 0;
        g_pactex[i] = 0;
    }
    for (i = 0; i < 4; i++) s_sizeAcBuff[i] = 0x239c00;
    for (i = 0; i < NowPlayerNum; i++) {
        g_pbufAc[i] = (char *)Memm_Alloc(1, s_sizeAcBuff[i] + (int)sizeof(StAcTextureBuffer), 0x10);
        g_pactex[i] = (StAcTextureBuffer *)(((unsigned int)g_pbufAc[i] + s_sizeAcBuff[i] + 0xf) & 0xfffffff0u);
    }
    /* end pointer; the original adds s_sizeAcBuff[n] (one past the AC it means: past the array when n == 4, which
     * reads the first word of ACTP2ObjectTableS) */
    unsigned int n = (unsigned int)i;
    g_pbufAc[n] = *(char **)((unsigned int)&g_pbufAc[0] + 4 * n - 4) + *(int *)((unsigned int)&s_sizeAcBuff[0] + 4 * n);
    return 1;
}

/* Parts file table: kind, CDVD file id base, psr, param struct in AcPartsDataSt and its size per part slot. */
static void __SetupLoadPartsInfo(StAcLoader *this_)
{
    static const struct { short kind; unsigned int cdvd; int psr; int ofs; int size; } tbl[16] = {
        {0, 0xebf0, 1, 0x0, 0x140},     {1, 0xeac4, 0, 0x140, 0x150},   {2, 0xed1c, 2, 0x290, 0x150},
        {3, 0xee48, 3, 0x3e0, 0x150},   {4, 0xef74, 4, 0x530, 0x130},   {5, 0xf0a0, -1, 0x660, 0x170},
        {6, 0xf1cc, -1, 0x7d0, 0x110},  {7, 0xf2f8, -1, 0x8e0, 0x110},  {8, 0xf424, 10, 0x9f0, 0x110},
        {9, 0xf550, 9, 0xb00, 0x130},   {10, 0xf8d4, 7, 0xc30, 0x140},  {10, 0xf8d4, 8, 0xd70, 0x140},
        {11, 0xf67c, 5, 0xeb0, 0x120},  {12, 0xf7a8, 6, 0xfd0, 0x140},  {11, 0xf67c, 11, 0x1110, 0x120},
        {12, 0xf7a8, 12, 0x1230, 0x140},
    };
    for (int i = 0; i < 16; i++) {
        StLoadParts *lp = &this_->m_tblParts[i];
        lp->m_kind = tbl[i].kind;
        lp->m_idCdvd = tbl[i].cdvd;
        lp->m_idParts = this_->m_pasmbly->idParts[i];
        lp->m_psr = tbl[i].psr;
        lp->m_pParam = (char *)this_->m_ppartsd + tbl[i].ofs;
        lp->m_sizeParam = tbl[i].size;
    }
}

/* Animation set id (1-based) from the core, arm and leg categories. */
int AcLoader_GetAnimIdFromCoreArmLeg(AcPartsDataSt *ppartsd)
{
    int animid = 0;
    switch (ppartsd->core.basic.category_id) {
    case 7: break;
    case 4: animid += 0x30; break;
    case 1: animid += 0x18; break;
    }
    switch (ppartsd->arms.basic.category_id) {
    case 7: case 5: break;
    case 3: animid += 0x10; break;
    case 1: animid += 8; break;
    }
    switch (ppartsd->legs.basic.category_id) {
    case 15: animid += 7; break;
    case 13: animid += 6; break;
    case 11: animid += 5; break;
    case 9: animid += 3; break;
    case 7: animid += 4; break;
    case 5: break;
    case 3: animid += 2; break;
    case 1: animid += 1; break;
    }
    return animid + 1;
}

int AcLoader_Init(void)
{
    PS2_FRAME(0x20);
    for (int i = 0; i < 4; i++) __InitAcModel(i);
    return 1;
}

/* Copies a 2 x 8 group of 4-byte CLUT blocks between a linear block buffer (idxBlock 0..0x3c over both rows) and a
 * paged PSMT4 CLUT image (64 blocks per row): isFromPage = page -> buffer, else buffer -> page. */
static void __CopyPageClutPsmt4(unsigned char *pdst, unsigned char *psrc, int xBlock, int yBlock, int isFromPage)
{
    int idxBlock = 0;
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < 8; i++, idxBlock += 4) {
            int idxPage = (xBlock + i + ((yBlock + j) << 6)) << 2;
            unsigned char *d = isFromPage ? pdst + idxBlock : pdst + idxPage;
            unsigned char *s = isFromPage ? psrc + idxPage : psrc + idxBlock;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
    }
}

/* the spare (shoulder-swap) weapon of hand ptsid becomes the active one: copies its saved texture page and CLUT
 * blocks back into the AC texture buffer (reverse of the copy-out in __LoadAcPartsOther) and, with the per-AC texture
 * areas (AcOpt.txx == 0), flags the side for re-upload */
void AcLoader_ChangeIntoSpareWepTexture(int acid, int ptsid)
{
    PS2_FRAME(0x30);
    StAcTextureBuffer *tex = g_pactex[acid];
    switch (ptsid) {
    case 0xf:
    case 0xd:
        CopyMemory((char *)&tex->texbuf + 0x3c000, tex->texbufSpwL, 0x2000);
        __CopyPageClutPsmt4((unsigned char *)&tex->clutbuf, (unsigned char *)tex->clutbufSpwL, 0x20, 0x10, 0);
        if (AcOpt.txx == 0) g_isSwTexTransL[acid] = 1;
        break;
    case 0xe:
    case 0xc:
        CopyMemory((char *)&tex->texbuf + 0x3a000, tex->texbufSpwR, 0x2000);
        __CopyPageClutPsmt4((unsigned char *)&tex->clutbuf, (unsigned char *)tex->clutbufSpwR, 0x38, 8, 0);
        if (AcOpt.txx == 0) g_isSwTexTransR[acid] = 1;
        break;
    }
}
