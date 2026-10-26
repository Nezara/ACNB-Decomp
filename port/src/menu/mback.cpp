/* src/menu/mback.cpp - MBACK, the menu background models (OLMENU): MBackMng holds up to three _stMModel (the
 * "tube" / garage room, MMD ids 0x4b0..0x4b2), their alphas, a small fade state machine (play_func =
 * __MBack_PlayFadeNX; stat 0 idle / 1 fade-in / 2 fade-out / 3 faded out), the MoverI spot-light alpha and the
 * scene-label stack (32 entries, two label mobjs: the current one and the one fading out, a 6-frame appear delay).
 * Every menu screen pushes / pops its label; DF_MBACK_Play runs once per menu frame. DF_MBACK_MakeVu1Packet
 * (phase 2 group 5a) appends the background models to the menu VIF1 chain. */
#include "acnb/ps2fp.h"

/* the inline "delete" of a label mobj (f_draw = 0, then f_erase = 1: two separate flag-byte writes) */
static inline void mback_erase(struct stMobj *p)
{
    p->f_draw = 0;
    p->f_erase = 1;
}

void DF_MBACK_ClearSceneLabel(void)
{
    if (MBackMng.pLabelCur) {
        mback_erase(MBackMng.pLabelCur);
        MBackMng.pLabelCur = 0;
    }
    if (MBackMng.pLabelFO) {
        mback_erase(MBackMng.pLabelFO);
        MBackMng.pLabelFO = 0;
    }
}

/* the label changed: the current label starts fading out (pLabelFO is not null-checked: SetFadeOut(NULL) when no
 * label mobj exists yet), the new one appears after 6 frames (none for label 0) */
static inline void mback_label_changed(signed char label)
{
    if (MBackMng.pLabelFO) {
        mback_erase(MBackMng.pLabelFO);
        MBackMng.pLabelFO = 0;
    }
    MBackMng.pLabelFO = MBackMng.pLabelCur;
    MBackMng.pLabelCur = 0;
    DF_MOBJ_SetFadeOut(MBackMng.pLabelFO);
    MBackMng.nAppearDelay = ((unsigned char)label != 0) ? 6 : 0;
}

void DF_MBACK_PopSceneLabel(void)
{
    PS2_FRAME(0x30);
    if (MBackMng.nLabelStackCur <= 0) return;
    MBackMng.nLabelStackCur--;
    int cur = MBackMng.nLabelStackCur;
    if ((unsigned char)MBackMng.aLabelStack[cur] == (unsigned char)MBackMng.aLabelStack[cur + 1]) return;
    signed char label = MBackMng.aLabelStack[cur];
    MBackMng.aLabelStack[MBackMng.nLabelStackCur] = label;   /* the original's self-store */
    mback_label_changed(label);
}

void DF_MBACK_PushSceneLabel(enum_5124d09c label)
{
    PS2_FRAME(0x30);
    if (!(MBackMng.nLabelStackCur < 0x1f)) return;
    MBackMng.nLabelStackCur++;
    MBackMng.aLabelStack[MBackMng.nLabelStackCur] = label;
    int cur = MBackMng.nLabelStackCur;
    if ((unsigned char)MBackMng.aLabelStack[cur] == (unsigned char)MBackMng.aLabelStack[cur - 1]) return;
    MBackMng.aLabelStack[MBackMng.nLabelStackCur] = label;   /* the original's self-store */
    mback_label_changed(label);
}

void DF_MBACK_SetHasRDraw(int inFlag) { MBackMng.f_draw_has_r = (char)inFlag; }

void DF_MBACK_SetSideDraw(int inFlag) { MBackMng.f_draw_side = (char)inFlag; }

void DF_MBACK_SetDraw(int inFlag) { MBackMng.f_draw = (char)(inFlag & 1); }

void DF_MBACK_MakeVu1Packet(void)
{
    PS2_FRAME(0x50);
    unsigned int *dmap = MPktCurrentp;
    PS2_STACK(int[3], jj, 0x40);   /* rodata copy {0, 1, 2} */
    jj[0] = 0;
    jj[1] = 1;
    jj[2] = 2;
    if (MBackMng.f_draw == 0) return;
    for (int i = 0; i < 3; i++) {
        int idx = jj[i];
        if (!MBackMng.mp[idx]) continue;
        if (MBackMng.f_draw_not_tube && idx != 0) continue;   /* only the room model */
        dmap = DF_MPKT_MakeGSRegister(dmap, 6 /* TEX0_1 */, 0x4000000045304000ULL);
        dmap = DF_MPKT_MakeGSRegister(dmap, 6, 0x6000000045304000ULL);
        dmap = DF_MMODEL_MakeVu1PacketAC(dmap, MBackMng.mp[idx], nullptr, i == 0 ? 7 : 3);
    }
    MPktCurrentp = dmap;
}

void DF_MBACK_Draw(void) {}

/* 1 when no fade is running (idle or faded out) */
int DF_MBACK_Sync(void)
{
    char stat = MBackMng.stat;
    return (stat == 0 || stat == 3) ? 1 : 0;
}

/* play_func: the model alpha fade (count_max steps of 1 / count_max * fade_alpha_st), then idle / faded out */
static void __MBack_PlayFadeNX(void)
{
    char stat = MBackMng.stat;
    if (stat == 2) {
        short c = MBackMng.count;
        MBackMng.count = (short)(c + 1);
        if (c < MBackMng.count_max) {
            for (int i = 0; i < 3; i++) {
                float step = ps2_div(-1.0f, (float)MBackMng.count_max);
                float x = MBackMng.fade_alpha[i] + step * MBackMng.fade_alpha_st[i];
                MBackMng.fade_alpha[i] = x;
                if (x < 0.0f) MBackMng.fade_alpha[i] = 0.0f;
            }
        } else {
            for (int i = 0; i < 3; i++) {
                if (!(MBackMng.fade_alpha[i] == 1.0f)) MBackMng.fade_alpha[i] = 0.0f;
            }
            MBackMng.stat = 3;
            MBackMng.play_func = 0;
        }
    } else if (stat == 1) {
        short c = MBackMng.count;
        MBackMng.count = (short)(c + 1);
        if (c < MBackMng.count_max) {
            for (int i = 0; i < 3; i++) {
                float step = ps2_div(1.0f, (float)MBackMng.count_max);
                float x = MBackMng.fade_alpha[i] + step * MBackMng.fade_alpha_st[i];
                MBackMng.fade_alpha[i] = x;
                if (!(x <= 1.0f)) MBackMng.fade_alpha[i] = 1.0f;
            }
        } else {
            for (int i = 0; i < 3; i++) {
                if (!(MBackMng.fade_alpha[i] == 0.0f)) MBackMng.fade_alpha[i] = 1.0f;
            }
            MBackMng.stat = 0;
            MBackMng.play_func = 0;
        }
    }
}

/* a texture scroll word is reset to 0 once |x| >= 1 (NaN too) */
static inline void mback_wrap_st(float *p)
{
    float f = *p;
    if (f < 0.0f) f = -f;
    if (!(f < 1.0f)) *p = 0.0f;
}

void DF_MBACK_Play(void)
{
    PS2_FRAME(0x40);
    if (MenuMng.f_help == 1) return;
    if (MBackMng.play_func) gcall(MBackMng.play_func);
    if (MBackMng.play_func_alpha) gcall(MBackMng.play_func_alpha);   /* never set */
    MoverI_Update(&MBackMng.SpotAlpha);
    for (int i = 0; i < 3; i++) {
        if (MBackMng.mp[i] == 0) continue;
        MBackMng.mp[i]->mulalpha = MBackMng.fade_alpha[i];
        DF_MMODEL_Play(MBackMng.mp[i]);
        if (i == 0) MBackMng.mp[0]->ang[2] = MBackMng.mp[0]->ang[2] + ps2_u2f(0x3a83126fu); /* 0.001f */
        mback_wrap_st(&MBackMng.mp[i]->addST[0]);
        mback_wrap_st(&MBackMng.mp[i]->addST[1]);
    }
    /* labels whose fade-out finished (stat 5) are deleted */
    if (MBackMng.pLabelCur && MBackMng.pLabelCur->stat == 5) {
        mback_erase(MBackMng.pLabelCur);
        MBackMng.pLabelCur = 0;
    }
    if (MBackMng.pLabelFO && MBackMng.pLabelFO->stat == 5) {
        mback_erase(MBackMng.pLabelFO);
        MBackMng.pLabelFO = 0;
    }
    if (MBackMng.nAppearDelay > 0) {
        MBackMng.nAppearDelay--;
        if (!(MBackMng.nAppearDelay > 0)) {
            MBackMng.pLabelCur = MenuUtil_NewSceneNameString(
                0, (unsigned char)MBackMng.aLabelStack[MBackMng.nLabelStackCur] + 0x19578);
            DF_MOBJ_SetDrawPrio(MBackMng.pLabelCur, 3);
            DF_MOBJ_SetFadeIn(MBackMng.pLabelCur);
        }
    }
}

void DF_MBACK_ChangeTag(void) {}

void DF_MBACK_SetSpotLightFadeOut(void) { MoverI_Move(&MBackMng.SpotAlpha, 0, 6); }

void DF_MBACK_SetSpotLightFadeIn(void) { MoverI_Move(&MBackMng.SpotAlpha, 0x80, 6); }

void DF_MBACK_SetFadeOutNX(void)
{
    for (int i = 0; i < 3; i++) {
        MBackMng.fade_alpha[i] = 1.0f;
        MBackMng.fade_alpha_st[i] = 1.0f;
    }
    MBackMng.play_func = GFN(__MBack_PlayFadeNX);
    MBackMng.stat = 2;
    MBackMng.count = 0;
    MBackMng.count_max = 0xf;
}

void DF_MBACK_SetFadeInNX(void)
{
    for (int i = 0; i < 3; i++) {
        if (!(MBackMng.fade_alpha[i] == 1.0f)) MBackMng.fade_alpha[i] = 0.0f;
        MBackMng.fade_alpha_st[i] = 1.0f;
    }
    MBackMng.play_func = GFN(__MBack_PlayFadeNX);
    MBackMng.stat = 1;
    MBackMng.count = 0;
    MBackMng.count_max = 7;
}

void DF_MBACK_Init(int inInitType, int inTagKind)
{
    PS2_FRAME(0xa0);
    PS2_STACK(float[4], pos, 0x80);
    PS2_STACK(float[4], ang, 0x70);
    PS2_STACK(float[4], scl, 0x60);
    PS2_STACK(struct struct_76b810ec, def_col, 0x94);
    PS2_STACK(struct struct_76b810ec, yuka_col, 0x98);
    PS2_STACK(struct struct_76b810ec, has_col, 0x9c);
    pos[0] = 0.0f;
    pos[1] = 0.0f;
    pos[2] = 0.0f;
    pos[3] = 1.0f;
    ang[0] = 0.0f;
    ang[1] = ps2_u2f(0x40490fdbu);   /* pi */
    ang[2] = 0.0f;
    ang[3] = 1.0f;
    scl[0] = 1.0f;
    scl[1] = 1.0f;
    scl[2] = 1.0f;
    scl[3] = 1.0f;
    has_col.r = has_col.g = has_col.b = has_col.a = 0;
    yuka_col.r = yuka_col.g = yuka_col.b = yuka_col.a = 0;
    def_col.r = def_col.g = def_col.b = def_col.a = 0;

    DF_MFUNC_ClearMemory(&MBackMng, 0x28);   /* 0x28 words = the whole struct */
    MBackMng.init_type = (char)inInitType;
    MBackMng.tag_kind = (char)inTagKind;
    MBackMng.alpha_side = 1.0f;
    MBackMng.tag_alpha = 0;
    MBackMng.tag_alpha_side = 0;
    MBackMng.f_draw = 1;
    MBackMng.f_draw_has_l = 1;
    MBackMng.f_draw_side = 0;
    MBackMng.f_draw_not_tube = 0;
    MBackMng.f_draw_has_r = 0;
    MBackMng.aLabelStack[0] = 0;
    MBackMng.nLabelStackCur = 0;
    for (int i = 0; i < 3; i++) {
        MBackMng.mp[i] = DF_MMODEL_GetNewModel(0);
        int id = 0x4b0 + i;
        struct stMmdData *mmd = DF_MMD_GetMMD(2, id);
        if (mmd == 0) {
            mmd = DF_MMD_GetNewMMD(0);
            DF_MMD_SetData(mmd, 2, id, DF_MMD_GetObjectMMD(id));
        }
        DF_MMODEL_SetData(MBackMng.mp[i], mmd, &pos, &ang, &scl);
        MBackMng.col_save[i] = def_col;
    }
    MBackMng.mp[2]->alpha_reg = 0x44;
    MBackMng.mp[1]->alpha_reg = 0x44;
    MBackMng.mp[0]->alpha_reg = 0x44;
    if (MBackMng.init_type == 1) MBackMng.alpha = 0.0f;
    MBackMng.addST[0] = ps2_u2f(0x3a23d70au);
    MBackMng.addST[1] = 0.0f;
    MBackMng.play_func = GFN(__MBack_PlayFadeNX);
    MBackMng.stat = 1;
    MBackMng.count_max = 7;
    MBackMng.nAppearDelay = 0;
    MoverI_Init(&MBackMng.SpotAlpha);
    MoverI_SetValue(&MBackMng.SpotAlpha, 0);
}
