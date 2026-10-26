/* src/ac/parts_opc.cpp - "opc" core special part (core_type 4, whole file, OLSORTIE overlay).
 * The pad (trigger conf 13) toggles the effect on / off. For 60 frames after a toggle the AC flickers (the bit pattern
 * of a drifting float "random" value picks visible / hidden), then holds; while on, infoOpc.alpha fades 1.0 -> 0.5
 * over frames 60-120. The result sets / clears the opc bit (use 0x2000) and pinfoOpc of the AC's seven models.
 * No Versus COM roster carries this core (s59 survey of rosters 1-18). */
#include "acnb/port.h"
#include "acnb/nodwarf.h"

/* original: s = s * 16.1f, stored, then s / 16.0f, stored; the caller tests the float's integer bits */
static unsigned int opc_rand_step(float *s)
{
    *s = *s * 16.1f;
    *s = *s / 16.0f;
    unsigned int bits;
    memcpy(&bits, s, 4);
    return bits;
}

static void opc_set_model(StModel *m, int vis, AcPartsOpcPrivateSt *prv)
{
    if (vis) {
        m->use |= 0x2000;
        m->pinfoOpc = &prv->infoOpc;
    } else {
        m->use &= 0xdfff;
        m->pinfoOpc = NULL;
    }
}

#ifdef ACNB_SOURCEPORT
float opc_frand[4] = {0x1.ff6f8ap+15f /* 0x477fb7c5 */, 0x1.9539c2p+11f /* 0x454a9ce1 */, 0x1.ff6f8ap+15f, 0x1.9539c2p+11f};
#endif

/* original quirk: the four fRand statics are never reset, so the flicker pattern carries over between matches and
 * drifts upward by x1.00625 per step. */
void AcOpc_Control(AcPartsOpcSt *opcp)
{
    PS2_FRAME(0x40);
    if (!opcp) return;
    AcPartsOpcPrivateSt *prv = opcp->prvp;
    AcPlayerSt *acp = prv->acp;
    int vis;
    if (prv->on) {
        int n = ++prv->count;
        if (n < 30) vis = (opc_rand_step(&opc_frand[0]) & 1) != 0;
        else if (n < 60) vis = (opc_rand_step(&opc_frand[1]) & 3) != 0;
        else if (n < 120) {
            vis = 1;
            prv->infoOpc.alpha = 1.0f - 0.5f * ((float)(n - 60) / 60.0f);
        } else {
            vis = 1;
            prv->infoOpc.alpha = 0.5f;
        }
        if (AcUidev_IsPadTrgConf(acp->frdp->uidevp, 0xd)) {
            prv->count = 0;
            prv->on = 0;
        }
    } else {
        int n = ++prv->count;
        if (n < 30) {
            unsigned int bits = opc_rand_step(&opc_frand[2]);
            prv->infoOpc.alpha = 1.0f;
            vis = (bits & 3) != 0;
        } else if (n < 60) vis = (opc_rand_step(&opc_frand[3]) & 1) != 0;
        else vis = 0;
        if (!AcCtrl_IsCharging(acp) && AcUidev_IsPadTrgConf(acp->frdp->uidevp, 0xd)) {
            prv->count = 0;
            prv->on = 1;
        }
    }
    opc_set_model(&MyAC[acp->id], vis, prv);
    opc_set_model(&MyRHW[acp->id], vis, prv);
    opc_set_model(&MyLHW[acp->id], vis, prv);
    opc_set_model(&MyLBW[acp->id], vis, prv);
    opc_set_model(&MyRBW[acp->id], vis, prv);
    opc_set_model(&MyLEX[acp->id], vis, prv);
    opc_set_model(&MyREX[acp->id], vis, prv);
}

void AcOpc_Init(AcPartsOpcSt *opcp, AcPlayerSt *acp)
{
    PS2_FRAME(0x40);
    if (!opcp) return;
    AcPartsOpcPrivateSt *prv = opcp->prvp;
    ZeroMemory8b(opcp, 4);
    ZeroMemory8b(prv, 0x18);
    opcp->prvp = prv;
    opcp->prvp->acp = acp;
}
