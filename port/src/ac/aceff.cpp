/* src/ac/aceff.cpp - per-AC attached effects: the _magic2 handles of the boost flames (14), blade trails (7), shield
 * fields (3) and weapon effects (4), plus 4 boost parameter sets taken from the parts' sfx combs. Register* start an
 * effect on an attach matrix (ObjCMatSt), Control* switch it on / off each frame and move it with the parent matrix *
 * attach matrix, Free* kill it. Driven by acsfx.c (per frame) and InitAcSfx / weapon / extension code (setup). */
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

/* the effect follows its attach point: mat = parent matrix * attach matrix */
static inline void aceff_follow(float (*mat)[4], const ObjCMatSt *oc)
{
    vu_mul_matrix(mat, *oc->parentmatp, oc->mat);
}

static inline AcPtsResSt *aceff_ptsres(AcEffSt *effp) { return effp->prvp->acp->frdp->ptsresp; }

int aceff_ControlWeapon(AcEffSt *effp, char wepm, int busy)
{
    PS2_FRAME(0x20);
    if (wepm < 0 || wepm >= 4) return 0;
    StAcEffSelectWeaponManage *m = &effp->prvp->wepmng[(int)wepm];
    if (m->pmag) {
        DF_MAGIC2_Boost(m->pmag, busy);
        aceff_follow(m->mat, m->pocm);
    }
    return 1;
}

int aceff_FreeWeapon(AcEffSt *effp, char wepm)
{
    PS2_FRAME(0x20);
    if (wepm < 0 || wepm >= 4) return 0;
    StAcEffSelectWeaponManage *m = &effp->prvp->wepmng[(int)wepm];
    if (m->pmag) {
        DF_MAGIC2_Kill(m->pmag);
        m->pmag = 0;
    }
    return 1;
}

/* Returns the slot used, -1 if none. Note: the search stops at the first slot that is *in use* (and overwrites it),
 * so with all four slots free it fails. */
char aceff_RegisterWeapon(AcEffSt *effp, int psr, int order, ObjCMatSt *objcmat)
{
    PS2_FRAME(0x70);
    char ireg = 0;
    do {
        if (effp->prvp->wepmng[(int)ireg].pmag) break;
        ireg++;
    } while (ireg < 4);
    if (ireg == 4) return -1;
    StAcEffSelectWeaponManage *m = &effp->prvp->wepmng[(int)ireg];
    AcPtsResSt *ptsresp = aceff_ptsres(effp);
    _stSfxCombHead *comb = AcPtsRes_GetSfxCombHead(ptsresp, psr);
    _magic2_data_st *magp = AcPtsRes_GetMagicPtr(ptsresp, psr, order);
    m->pmag = magp ? DF_MAGIC2_AppearBoostPtr(magp, comb, 0, &m->mat, 4) : 0;
    m->frame = 0;
    m->pocm = objcmat;
    return ireg;
}

int aceff_ControlShield(AcEffSt *effp, int shieldm, int busy)
{
    PS2_FRAME(0x20);
    if (shieldm < 0 || shieldm >= 3) return 0;
    struct_d6b7e912 *m = &effp->prvp->shieldmng[shieldm];
    if (m->mag2p) {
        DF_MAGIC2_Boost(m->mag2p, busy);
        aceff_follow(m->mat, m->ocmp);
    }
    return 1;
}

int aceff_FreeShield(AcEffSt *effp, int shieldm)
{
    PS2_FRAME(0x20);
    if (shieldm < 0 || shieldm >= 3) return 0;
    struct_d6b7e912 *m = &effp->prvp->shieldmng[shieldm];
    if (m->mag2p) {
        DF_MAGIC2_Kill(m->mag2p);
        m->mag2p = 0;
    }
    return 1;
}

int aceff_RegisterShield(AcEffSt *effp, int shieldm, int psr, int order, ObjCMatSt *objcmat)
{
    PS2_FRAME(0x60);
    if (shieldm < 0 || shieldm >= 3) return 0;
    struct_d6b7e912 *m = &effp->prvp->shieldmng[shieldm];
    AcPtsResSt *ptsresp = aceff_ptsres(effp);
    _stSfxCombHead *comb = AcPtsRes_GetSfxCombHead(ptsresp, psr);
    _magic2_data_st *magp = AcPtsRes_GetMagicPtr(ptsresp, psr, order);
    m->mag2p = magp ? DF_MAGIC2_AppearBoostPtr(magp, comb, 0, &m->mat, 4) : 0;
    m->frame = 0;
    m->ocmp = objcmat;
    return 1;
}

struct_d6b7e912 *aceff_GetShield(AcEffSt *effp, int shieldm)
{
    return (shieldm < 0 || shieldm >= 3) ? 0 : &effp->prvp->shieldmng[shieldm];
}

int aceff_FreeBlade(AcEffSt *effp, int bladem)
{
    PS2_FRAME(0x20);
    if (bladem < 0 || bladem >= 7) return 0;
    struct_3956eba2 *b = &effp->prvp->blademng[bladem];
    if (b->mag2p) {
        DF_MAGIC2_Kill(b->mag2p);
        b->mag2p = 0;
    }
    if (b->magTp) {
        DF_MAGIC2_Kill(b->magTp);
        b->magTp = 0;
    }
    return 1;
}

/* the blade glow follows its attach point; the trail (magTp) keeps burning 6 frames after the blade stops */
int aceff_ControlBlade(AcEffSt *effp, int bladem, int burning)
{
    PS2_FRAME(0x30);
    if (bladem < 0 || bladem >= 7) return 0;
    struct_3956eba2 *b = &effp->prvp->blademng[bladem];
    if (b->mag2p && b->ocmp->parentmatp) {
        DF_MAGIC2_Boost(b->mag2p, burning);
        aceff_follow(b->mat, b->ocmp);
    }
    if (b->magTp) {
        if (burning) b->trdlay = 6;
        else if (b->trdlay > 0) b->trdlay--;
        DF_MAGIC2_Boost(b->magTp, b->trdlay > 0);
    }
    return 1;
}

int aceff_AppearBlade(AcEffSt *effp, int bladem)
{
    if (bladem < 0 || bladem >= 7) return 0;
    struct_3956eba2 *b = &effp->prvp->blademng[bladem];
    b->frame = 0;
    b->flag = 0;
    return 1;
}

/* blades 0 / 1 (the arm blades) also get a trail effect (magic 0x1fc + AC id) in the blade's colour */
int aceff_RegisterBlade(AcEffSt *effp, int bladem, int psr, int order, ObjCMatSt *ocmp)
{
    PS2_FRAME(0x80);
    if (bladem < 0 || bladem >= 7) return 0;
    struct_3956eba2 *b = &effp->prvp->blademng[bladem];
    AcPtsResSt *ptsresp = aceff_ptsres(effp);
    _stSfxCombHead *comb = AcPtsRes_GetSfxCombHead(ptsresp, psr);
    _magic2_data_st *magp = AcPtsRes_GetMagicPtr(ptsresp, psr, order);
    b->mag2p = magp ? DF_MAGIC2_AppearBoostPtr(magp, comb, 0, &b->mat, 4) : 0;
    b->frame = 0;
    b->ocmp = ocmp;
    b->hcoll = -1;
    b->flag = 0;
    b->magTp = 0;
    if (bladem == 0 || bladem == 1) {
        b->magTp = DF_MAGIC2_AppearBoost((unsigned short)(effp->prvp->acp->id + 0x1fc), 0, &b->mat, 0);
        if (b->mag2p && b->magTp)
            for (int k = 0; k < 4; k++) b->magTp->rgba[k] = (float)b->mag2p->rgba0[k];
    }
    return 1;
}

struct_3956eba2 *aceff_GetBlade(AcEffSt *effp, int bladem)
{
    return (bladem < 0 || bladem >= 7) ? 0 : &effp->prvp->blademng[bladem];
}

/* boost parameter set from the parts' sfx comb: colour = colour key (percent) of the magic's base colour */
void aceff_SetBoostParam(AcEffSt *effp, int bprm)
{
    PS2_FRAME(0x40);
    if (bprm < 0 || bprm >= 4) return;
    struct_149f3362 *p = &effp->prvp->boostparam[bprm];
    int psr, order;
    __GetBoostParamToPsr(bprm, &psr, &order);
    AcPtsResSt *ptsresp = aceff_ptsres(effp);
    _stSfxCombHead *comb = AcPtsRes_GetSfxCombHead(ptsresp, psr);
    _magic2_data_st *magp = AcPtsRes_GetMagicPtr(ptsresp, psr, order);
    if (!magp) return;
    p->umblparam.sfxid = 0;
    p->umblparam.ptype = 1;
    p->umblparam.magp = magp;
    p->umblparam.combp = comb;
    for (int k = 0; k < 4; k++)   /* (the original first stores 128.0 in all four, then overwrites them) */
        p->umblparam.color[k] = (float)((int)magp->col_key[1][k] * (int)magp->color0[k] / 100);
}

/* colour lanes 0-2 of both flame parts */
void aceff_CopyBoostColor(AcEffSt *effp, int destbprm, int scrbprm)
{
    if (destbprm < 0 || destbprm >= 4 || scrbprm < 0 || scrbprm >= 4) return;
    struct_149f3362 *d = &effp->prvp->boostparam[destbprm], *s = &effp->prvp->boostparam[scrbprm];
    for (int k = 0; k < 3; k++) d->umblparam.color[k] = s->umblparam.color[k];
    for (int k = 0; k < 3; k++) d->tailparam.color[k] = s->tailparam.color[k];
}

int aceff_ControlBoost(AcEffSt *effp, int boostm, int boost)
{
    PS2_FRAME(0x50);
    if (boostm < 0 || boostm >= 14) return 0;
    _aceff_boostmng *m = &effp->prvp->boostmng[boostm];
    if (!m->parentmatp) return 0;
    if (!m->basematp) return 0;
    vu_mul_matrix(m->mat, *m->parentmatp, *m->basematp);
    for (int ii = 0; ii < 4; ii++)
        if (m->mag2p[ii]) DF_MAGIC2_Boost(m->mag2p[ii], boost);
    if (m->mag2p[0]) {   /* else colorp / alpha keep their previous values */
        m->colorp = &m->mag2p[0]->rgba;
        m->alpha = m->mag2p[0]->rgba[3];
    }
    return 1;
}

void aceff_FreeBoost(AcEffSt *effp, int boostm)
{
    PS2_FRAME(0x30);
    if (boostm < 0 || boostm >= 14) return;
    _aceff_boostmng *m = &effp->prvp->boostmng[boostm];
    for (int ii = 0; ii < 4; ii++)
        if (m->mag2p[ii]) {
            DF_MAGIC2_Kill(m->mag2p[ii]);
            m->mag2p[ii] = 0;
        }
}

/* Up to four flames per booster (mask bit set = none): even ones from the umbilical part, odd ones from the tail
 * part; flames 0 / 1 get option 0x200 as well. setcolor: all four take the umbilical colour. */
int aceff_RegisterBoost(AcEffSt *effp, int boostm, float (*parentmatp)[4][4], float (*basematp)[4][4], unsigned int mask,
                        int setcolor)
{
    PS2_FRAME(0x80);
    PS2_STACK(int[4], color, 0x70);   /* handed to DF_MAGIC2_BoostColorChange */
    if (boostm < 0 || boostm >= 14) return 0;
    _aceff_boostmng *m = &effp->prvp->boostmng[boostm];
    struct_149f3362 *p = &effp->prvp->boostparam[__GetBoostMngToBoostParamIndex(boostm)];
    m->alpha = 0.0f;
    m->colorp = 0;
    m->parentmatp = parentmatp;
    m->basematp = basematp;
    sceVu0CopyMatrix(m->mat, IdentityMtx);
    for (int i = 0; i < 4; i++) {
        if ((mask >> i) & 1) {
            m->mag2p[i] = 0;
            continue;
        }
        struct_68f5da36 *sub = (i & 1) ? &p->tailparam : &p->umblparam;
        unsigned short option = (unsigned short)(((i & 2) ? 0 : 0x200) | 4);
        if (sub->ptype) m->mag2p[i] = DF_MAGIC2_AppearBoostPtr(sub->magp, sub->combp, 0, &m->mat, option);
        else m->mag2p[i] = DF_MAGIC2_AppearBoost((unsigned short)sub->sfxid, 0, &m->mat, option);
    }
    if (setcolor) {
        for (int k = 0; k < 4; k++) color[k] = ps2_ftoi(p->umblparam.color[k]);
        for (int i = 0; i < 4; i++)
            if (m->mag2p[i]) DF_MAGIC2_BoostColorChange(m->mag2p[i], color);
    }
    return 1;
}

_aceff_boostmng *aceff_GetBoost(AcEffSt *effp, int boostm) { return &effp->prvp->boostmng[boostm]; }   /* no range check */

int aceff_Init(AcEffSt *effp, AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    if (!effp) return 0;
    AcEffPrivateSt *prv = effp->prvp;
    ZeroMemory8b(effp, 0x10);
    ZeroMemory8b(prv, 0xb60);
    effp->prvp = prv;
    effp->prvp->acp = acp;
    return 1;
}

/* boost parameter set -> parts sfx resource / magic order: main booster, back booster, extension, shoulder */
static void __GetBoostParamToPsr(int bparam, int *psrp, int *orderp)
{
    switch (bparam) {
    case 0: *psrp = 4; *orderp = 0; break;
    case 1: *psrp = 3; *orderp = 0; break;
    case 2: *psrp = 9; *orderp = 1; break;
    case 3: *psrp = 7; *orderp = 1; break;
    default: *psrp = -1; *orderp = -1; break;
    }
}

/* booster slot -> parameter set: 0-1 main, 2-7 legs / sub boosters, 8-11 extension, 12-13 shoulder */
static int __GetBoostMngToBoostParamIndex(int boostm)
{
    if (boostm >= 0 && boostm < 2) return 0;
    if (boostm >= 2 && boostm < 6) return 1;
    if (boostm >= 6 && boostm < 8) return 1;
    if (boostm >= 8 && boostm < 0xc) return 2;
    if (boostm >= 0xc && boostm < 0xe) return 3;
    return -1;
}
