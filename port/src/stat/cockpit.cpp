/* src/stat/cockpit.cpp - cockpit view panel (whole file): the cockpit model drawn with its monitor showing the gauge
 * screen (rendered to a 256x128 off-screen buffer by the TP2 attribute callback), after clearing the shadow
 * buffer. CKPT_Create sets the panel defaults (camera fov 36.5, position (0, 1750, 8700), alpha 0x5a). */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#define A16 __attribute__((aligned(16)))

/* DF_DrawModel_TP2 attribute callback: renders the gauge screen (command list per maker, enemy info) into the
 * monitor texture, then sets up the panel texture registers */
static int set_model_attribute(StModel *mp, int id, void *argp)
{
    PS2_FRAME(0x50);
    StCockpitMng *mng = (StCockpitMng *)argp;
    int c[4] A16 = {0x80, 0x80, 0x80, 0x80};
    Draw_SetupPreRenderFrameBuffer(0x1da, 2, 0x100, 0x80);
    if (!mng->paneloff) {
        GSX_SetWindowSize(0x80, 0x40, 4, 4);
        draw_cmdlist(mng->maker == 1 ? draw_stat_bco_tab[id] : draw_stat_aco_tab[id]);
    }
    Gauge_DrawEnemyInfo(0, ginfop, 1);
    Draw_RestoreFrameBuffer();
    sceGsSyncPath(0, 0);
    int blend = mng->blend;
    int lerp = mng->lerp;
    unsigned long long tex0 = mng->tex0;
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 8), 8); GS_NewList();
    GS_SetTEXFLUSH();
    GS_SetRegister(6, tex0);                  /* TEX0_1 */
    GS_SetRegister(0x14, lerp ? 0x60 : 0);    /* TEX1_1: bilinear */
    GS_SetRegister(8, 0);                     /* CLAMP_1 */
    GS_SetRegister(0x47, 0x30000);            /* TEST_1 */
    GS_SetBlendMode(blend);
    GS_SetRGBA(c);
    GS_SetTEXA(0x80, 1, 0x80);
    GS_FlushList(); ps2_rq_tag_end();
    return 0;
}

static void draw_cockpit_panel(StCockpitMng *mng)
{
    PS2_FRAME(0x30);
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 8), 8); GS_NewList();
    GS_SetTEXFLUSH();
    GS_SetRegister(6, mng->tex0);
    StatTex_SetDrawConfigure(mng->lerp != 0);
    GS_SetRegister(0x14, mng->lerp ? 0x60 : 0);
    GS_SetRegister(0x14, 0x60);
    GS_SetRegister(8, 0);
    GS_SetRegister(0x47, 0x30000);
    GS_SetBlendMode(mng->blend);
    int c[4] A16 = {0x80, 0x80, 0x80, 0x80};
    GS_SetRGBA(c);
    DF_ACSTAT_DrawSprite(0x10, 0x40, 0.0f, 0.0f, 0x100, 0x100);
    GS_FlushList(); ps2_rq_tag_end();
    DF_SetWorldScreenMatrix(mng->fovy);
    DF_SetPrimColor(mng->rgba);
    DF_DrawModel_TP2(mng->mp, GFN(set_model_attribute), mng);
}

void CKPT_Draw(StCockpitMng *mng)
{
    PS2_FRAME(0x70);
    StModel *mp = mng->mp;
    int c[4] A16 = {0x80, 0x80, 0x80, 0x80};
    DF_KickPreReadyShadow();
    sceGsSyncPath(0, 0);
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 8), 8); GS_NewList();
    GS_SetRegister(6, 0x8007ed0621413940ull);
    GS_SetRegister(0x14, 0);
    GS_SetRegister(8, 0x4010);
    GS_SetRegister(0x47, 0x30000);
    GS_SetBlendMode(0);
    GS_SetRGBA(c);
    GS_DrawSpriteDP(-0x800, -0x800, 0x800, 0x800, 0, 0, 0x1000, 0x1000, 0);
    GS_FlushList(); ps2_rq_tag_end();
    DF_KickPostReadyShadow();
    sceGsSyncPath(0, 0);
    memcpy(mp->loc, mng->loc, 16);
    memcpy(mp->ang, mng->ang, 16);
    memcpy(mp->scale, mng->scl, 16);
    DF_MakeModelLocalMatrixZXY_Model(mp, mp->loc, mp->ang, mp->scale);
    for (int i = 0; i < mp->nobj; i++) Anim_UpdateMyObjMatrix(&mp->op[i], mp->matrix2);
    draw_cockpit_panel(mng);
}

void CKPT_Create(StCockpitMng *mng, StModel *mp)
{
    PS2_FRAME(0x30);
    memset(mng, 0, 0x60);
    mng->mp = mp;
    mng->lerp = 1;
    mng->tex0 = 0x5e0213b40ull;
    mng->fovy = 36.5f;
    mng->loc[1] = 1750.0f;
    mng->loc[2] = 8700.0f;
    mng->scl[0] = 1.0f;
    mng->scl[1] = 1.0f;
    mng->scl[2] = 1.0f;
    mng->scl[3] = 1.0f;
    mng->rgba[0] = 0xff;
    mng->rgba[1] = 0xff;
    mng->rgba[2] = 0xff;
    mng->rgba[3] = 0x5a;
    for (int i = 0; i < mp->nobj; i++) memcpy(mp->op[i].scale, UnitVectorF, 16);
    memcpy(mp->scale, UnitVectorF, 16);
}
