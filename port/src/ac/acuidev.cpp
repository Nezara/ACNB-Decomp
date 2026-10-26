/* src/ac/acuidev.cpp - per-AC input device: the pad state the game logic reads (buttons, edges, four sticks), fed
 * each frame from the compact pad data through an 8-entry key delay ring (or from injected event keys), and the key
 * configuration that maps each logical function slot to a pad-button mask (plus the analog stick assignment). Also
 * the analog helpers of the leg code (walk speed rate / slow walk from the stick deflection). */
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
struct_e31718e7 AcUidev_SetKeyConfig__keyid2pad[14] = {
    {0, {0}, 0x0010}, {1, {0}, 0x0040}, {2, {0}, 0x0080}, {3, {0}, 0x0020}, {4, {0}, 0x0400}, {0xf, {0}, 0x0800},
    {5, {0}, 0x0100}, {0x10, {0}, 0x0200}, {0xe, {0}, 0x2000}, {0xc, {0}, 0x4000}, {0xb, {0}, 0x1000},
    {0xd, {0}, 0x8000}, {6, {0}, 0x0002}, {0x11, {0}, 0x0004}};
StCompactPadData AcUidev_Update__s_paddNeutral = {0, 8, 8, 8, 8};
#endif

/* walk speed rate from the stick deflection beyond 59 along the walk direction (dir 0..7: forward, forward-left,
 * forward-right, left, right, back, back-left, back-right), at most 1; scales *paccel too */
float AcUidev_ApplyAnalogLegRate(int dir, char alh, char alv, float rateMin, float *paccel)
{
    auto term = [&](int x) { return rateMin + ((float)x / 61.0f) * (1.0f - rateMin); };
    float v = 0.0f, h = 0.0f;
    if ((unsigned int)dir < 8) {
        switch (dir) {
        case 0: case 1: case 2:
            if (alv < -59) v = term(-59 - alv);
            break;
        case 5: case 6: case 7:
            if (alv >= 60) v = term(alv - 59);
            break;
        }
        switch (dir) {
        case 1: case 3: case 6:
            if (alh < -59) h = term(-59 - alh);
            break;
        case 2: case 4: case 7:
            if (alh >= 60) h = term(alh - 59);
            break;
        }
    }
    float r = h + v;
    if (!(r <= 1.0f)) r = 1.0f;
    if (paccel) *paccel = *paccel * r;
    return r;
}

/* the stick is not pushed past 89 along the walk direction */
int AcUidev_IsAnalogSlowlyLeg(int dir, char alh, char alv)
{
    switch (dir) {
    case 0: return alv >= -89;
    case 1: return alh >= -89 && alv >= -89;
    case 2: return alh < 90 && alv >= -89;
    case 3: return alh >= -89;
    case 4: return alh < 90;
    case 5: return alv < 90;
    case 6: return alh >= -89 && alv < 90;
    case 7: return alh < 90 && alv < 90;
    }
    return 0;
}

int AcUidev_IsMoving(AcUidevSt *uidevp)
{
    AcUidevPrivateSt *p = uidevp->prvp;
    if (p->padmode == 1) {
        char s0 = uidevp->padd.stick[p->stickcnf.assign[0]], s1 = uidevp->padd.stick[p->stickcnf.assign[1]];
        return s0 >= 60 || s0 < -59 || s1 >= 60 || s1 < -59;
    }
    const unsigned short *k = p->keycnf[p->padmode];
    unsigned short on = uidevp->padd.on;
    return (on & k[0]) || (on & k[1]) || (on & k[8]) || (on & k[9]);
}

/* stick 3 is negated unless reversed (not range-checked) */
char AcUidev_GetPadStickConf(AcUidevSt *uidevp, int stick)
{
    if (stick == 3 && uidevp->prvp->stickcnf.reverse == 0)
        return (char)-uidevp->padd.stick[uidevp->prvp->stickcnf.assign[3]];
    return uidevp->padd.stick[uidevp->prvp->stickcnf.assign[stick]];
}

int AcUidev_IsPadOnMaskedEqual(AcUidevSt *uidevp, unsigned int mask, unsigned int btn)
{
    return btn == (uidevp->padd.on & mask);
}

int AcUidev_IsPadTrgConf(AcUidevSt *uidevp, unsigned int btn)
{
    AcUidevPrivateSt *p = uidevp->prvp;
    return (uidevp->padd.trg & p->keycnf[p->padmode][btn]) != 0;
}

int AcUidev_IsPadOnConf(AcUidevSt *uidevp, unsigned int btn)
{
    AcUidevPrivateSt *p = uidevp->prvp;
    return (uidevp->padd.on & p->keycnf[p->padmode][btn]) != 0;
}

int AcUidev_IsAnalog(AcUidevSt *uidevp) { return uidevp->prvp->padmode == 1; }

/* extension turn booster: press one turn key, release the other (trg untouched) */
void AcUidev_SetFakeTurn(AcUidevSt *uidevp, int isLeft)
{
    AcUidevPrivateSt *p = uidevp->prvp;
    int on = isLeft ? 3 : 2, off = isLeft ? 2 : 3;
    uidevp->padd.on = uidevp->padd.on & (unsigned short)~p->keycnf[p->padmode][off];
    p = uidevp->prvp;
    uidevp->padd.on = uidevp->padd.on | p->keycnf[p->padmode][on];
}

/* script-injected keys (bit i = function slot i), read by Update while the pad is off */
void AcUidev_SetEventKey(AcUidevSt *uidevp, unsigned short key)
{
    AcUidevPrivateSt *p = uidevp->prvp;
    p->inpadd.on = 0;
    for (int i = 0; i < 0x11; i++)
        if (key & (1 << i)) p->inpadd.on = p->inpadd.on | p->keycnf[p->padmode][i];
}

void AcUidev_SetKeyDeray(AcUidevSt *uidevp, unsigned short deray) { uidevp->prvp->key_deray = deray; }

/* key_deray is kept */
void AcUidev_ClearKeybuff(AcUidevSt *uidevp)
{
    PS2_FRAME(0x40);
    ZeroMemory(&uidevp->padd, 8);
    ZeroMemory(&uidevp->prvp->inpadd, 8);
    uidevp->prvp->key_windex = 0;
    for (unsigned int k = 0; k < 8; k++) ZeroMemory(&uidevp->prvp->keybuff[k], 8);
}

/* One frame: this frame's pad (neutral after area-over in mode 7, the injected keys while the pad is off) goes into
 * the delay ring; the game sees the entry key_deray frames old (nothing once AP is 0), with trg = newly pressed. */
void AcUidev_Update(AcUidevSt *uidevp, StCompactPadData *ppadd)
{
    PS2_FRAME(0x40);
    PS2_STACK(StAcPadData, padd, 0x38);   /* trg is never written on the pad path: stale stack goes into keybuff */
    AcUidevPrivateSt *p = uidevp->prvp;
    AcPlayerSt *acp = p->acp;
    if (acp->areaovered && g_sortie.pconstant->modeSortie == 7) ppadd = &AcUidev_Update__s_paddNeutral;
    if (acp->stifftime > 0) CopyMemory(&padd, &p->inpadd, 8);
    if (MyI.padoff == 1) {
        CopyMemory(&padd, &uidevp->prvp->inpadd, 8);
        ZeroMemory(&uidevp->prvp->inpadd, 8);
    } else {
        const unsigned char *b = (const unsigned char *)ppadd;
        padd.on = ppadd->bits;
        padd.stick[0] = (char)((b[2] & 0xf) * 16 - 0x78);
        padd.stick[1] = (char)((b[2] >> 4) * 16 - 0x78);
        padd.stick[2] = (char)((b[3] & 0xf) * 16 - 0x78);
        padd.stick[3] = (char)((b[3] >> 4) * 16 - 0x78);
    }
    p = uidevp->prvp;
    CopyMemory(&p->keybuff[p->key_windex], &padd, 8);
    p = uidevp->prvp;
    unsigned int old = p->key_windex;
    unsigned int idx = (old - p->key_deray) & 7;
    p->key_windex = (unsigned short)(old + 1);
    p->key_windex = (unsigned short)((old + 1) & 7);
    p = uidevp->prvp;
    unsigned short oldOn = uidevp->padd.on;
    if (p->acp->hp) CopyMemory(&uidevp->padd, &p->keybuff[idx], 8);
    else ZeroMemory(&uidevp->padd, 8);
    uidevp->padd.trg = (unsigned short)(~oldOn & uidevp->padd.on);
}

/* Key configuration: for each function slot, the pad button the user assigned to its function id (the first physical
 * button index carrying it, mapped through keyid2pad); analog mode also takes the stick assignment from buttons 7 /
 * 0x12 (view stick, reversed or not) and 9 / 0x14 (move stick). */
void AcUidev_SetKeyConfig(AcUidevSt *uidevp, StKeyConfig *pkeyconf)
{
    static const unsigned char slot_func[17][2] = {   /* {function id, keycnf slot} in the original's order */
        {0, 0}, {1, 1}, {2, 2}, {3, 3}, {6, 4}, {5, 5}, {7, 6}, {4, 7}, {8, 8}, {9, 9},
        {0xa, 0xa}, {0xb, 0xb}, {0xc, 0xd}, {0xd, 0xe}, {0xe, 0x10}, {0xf, 0xf}, {0x10, 0xc}};
    AcUidevPrivateSt *p = uidevp->prvp;
    p->padmode = pkeyconf->mode ? 0 : 1;
    for (int s = 0; s < 17; s++) {
        const char *as = pkeyconf->assign[pkeyconf->mode];
        int i;
        for (i = 0; i < 0x16; i++)
            if (as[i] == slot_func[s][0]) break;
        unsigned short padid = 0;
        if (i < 0x16)
            for (unsigned int k = 0; k < 0xe; k++)
                if (i == AcUidev_SetKeyConfig__keyid2pad[k].keyid) {
                    padid = AcUidev_SetKeyConfig__keyid2pad[k].padid;
                    break;
                }
        p->keycnf[p->padmode][slot_func[s][1]] = padid;
    }
    if (p->padmode != 1) return;
    StAcUidevStickConfig *sc = &p->stickcnf;
    const char *as = pkeyconf->assign[pkeyconf->mode];
    switch (as[7]) {
    case 0xb: sc->assign[3] = 1; sc->reverse = 1; break;
    case 0xa: sc->assign[3] = 1; sc->reverse = 0; break;
    case 0: sc->assign[1] = 1; break;
    }
    switch (as[0x12]) {
    case 0xb: sc->assign[3] = 3; sc->reverse = 1; break;
    case 0xa: sc->assign[3] = 3; sc->reverse = 0; break;
    case 0: sc->assign[1] = 3; break;
    }
    switch (as[9]) {
    case 8: sc->assign[2] = 2; break;
    case 2: sc->assign[0] = 2; break;
    }
    switch (as[0x14]) {
    case 8: sc->assign[2] = 0; break;
    case 2: sc->assign[0] = 0; break;
    }
}

int AcUidev_Init(AcUidevSt *uidevp, AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    if (!uidevp) return 0;
    AcUidevPrivateSt *p = uidevp->prvp;
    ZeroMemory8b(uidevp, 0xc);
    ZeroMemory8b(p, 0xa0);
    uidevp->prvp = p;
    uidevp->prvp->acp = acp;
    return 1;
}
