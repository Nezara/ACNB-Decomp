/* src/ac/acinst.cpp — per-AC instance memory: AcInst_Init wipes the AC's AcPlayerSt and its AcInstantSt and links the
 * sub-module public structs to their private halves; the AcInst_Get* accessors return the public part structs inside
 * the AC's AcInstPartsSt (acp->prvp->partsp). */

struct AcPartsDataSt *AcInst_GetAllPartsParam(struct AcPlayerSt *acp) { return &acp->prvp->partsp->partsdAll; }
struct AcPartsBladeSt *AcInst_GetPartsBlade(struct AcPlayerSt *acp) { return &acp->prvp->partsp->pub_blade; }
struct AcPartsRbladeSt *AcInst_GetPartsRblade(struct AcPlayerSt *acp) { return &acp->prvp->partsp->pub_rblade; }
struct AcPartsLgunSt *AcInst_GetPartsLgun(struct AcPlayerSt *acp) { return &acp->prvp->partsp->pub_lgun; }
struct AcPartsExtSt *AcInst_GetPartsExt(struct AcPlayerSt *acp) { return &acp->prvp->partsp->pub_ext; }
struct AcPartsEoSt *AcInst_GetPartsEo(struct AcPlayerSt *acp) { return &acp->prvp->partsp->pub_coreEo; }
struct AcPartsObSt *AcInst_GetPartsOb(struct AcPlayerSt *acp) { return &acp->prvp->partsp->pub_coreOb; }
struct AcPartsOpcSt *AcInst_GetPartsOpc(struct AcPlayerSt *acp) { return &acp->prvp->partsp->pub_coreOpc; }
struct AcPartsSlowSt *AcInst_GetPartsSlow(struct AcPlayerSt *acp) { return &acp->prvp->partsp->pub_coreSlow; }
struct AcPartsGazeSt *AcInst_GetPartsGaze(struct AcPlayerSt *acp) { return &acp->prvp->partsp->pub_coreGaze; }   /* offset 0 */

void AcInst_Init(int acid)
{
    AcInstantSt *im = &s_acinstantMemory[acid];
    ZeroMemory8b(&g_ac[acid], 0x2600);
    ZeroMemory8b(im, 0x50f0);
    im->pub_parts.pub_rblade.prvp = &im->pub_parts.prv_rblade;
    im->pub_parts.pub_blade.prvp = &im->pub_parts.prv_blade;
    im->pub_parts.pub_lgun.prvp = &im->pub_parts.prv_lgun;
    im->pub_parts.pub_coreGaze.prvp = &im->pub_parts.prv_coreGaze;
    im->pub_parts.pub_coreSlow.prvp = &im->pub_parts.prv_coreSlow;
    im->pub_parts.pub_coreOpc.prvp = &im->pub_parts.prv_coreOpc;
    im->pub_parts.pub_coreOb.prvp = &im->pub_parts.prv_coreOb;
    im->pub_parts.pub_coreEo.prvp = &im->pub_parts.prv_coreEo;
    im->pub_parts.pub_ext.prvp = &im->pub_parts.prv_ext;
    im->pub_eff.prvp = &im->prv_eff;
    im->pub_act.prvp = &im->prv_act;
    im->pub_wep.prvp = &im->prv_wep;
    im->pub_uidev.prvp = &im->prv_uidev;
    im->pub_ptsres.prvp = &im->prv_ptsres;
    im->pub_ptsdstry.prvp = &im->prv_ptsdstry;
    im->prv_player.effp = &im->pub_eff;
    im->prv_player.uidevp = &im->pub_uidev;
    im->prv_player.sep = &im->pub_se;
    im->prv_player.sfxp = &im->pub_sfx;
    im->prv_player.actp = &im->pub_act;
    im->prv_player.wepp = &im->pub_wep;
    im->prv_player.ptsresp = &im->pub_ptsres;
    im->prv_player.ptsdstryp = &im->pub_ptsdstry;
    im->prv_player.partsp = &im->pub_parts;
    g_ac[acid].prvp = &im->prv_player;
}
