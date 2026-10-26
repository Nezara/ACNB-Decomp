/* src/menu/mtitle.cpp - the title screen (OLMENU): one stMobj (MTitle_NewSystem, called by mmanage) whose play_func
 * walks the US logo -> FS logo -> opening movie -> title scene (BG, logo shadow, logo, copyright, ten flashing
 * sprites run by a second "flash" stMobj, blinking PRESS START) -> 4-row menu (NEW GAME / LOAD GAME / VERSUS /
 * CONVERT) -> decide (MenuMng.ret = next screen) and tear-down. 900 idle frames on PRESS START fade the title away
 * and restart the logos (attract loop). The title work struct_2749aef8 is INLINE at obj+0x10 (DWARF has no dt union
 * member for it); the flash object's work struct_3e82413b likewise.
 * g1 (native): the flash cluster (__MenuTitle_FlashInit / FlashFadeIn / Flash / FlashFadeOut / FlashHide), the movie
 * state (__MenuTitle_SetPlayMovie), the title build (__MenuTitleInitNoFadeLogo, __MenuTitleInit), __MenuTitle_FadeInPlay,
 * __MenuTitle_WaitStartButton, __MenuTitle_MoveCursor, __MenuTitle_InitPhaseCancel / PlayPhaseCancel and
 * __MenuTitle_StartFadeOut (attract tear-down on a copy of the work).
 * g2 (native): the six US / FS logo states (__MenuTitle_*LogoFadeIn / Play / FadeOut), USLogoInit / FSLogoInit,
 * MTitle_NewSystem.
 * Original quirks kept: the logo FadeIn skip arms keep timer (FadeOut alpha jumps); FlashFadeOut passes the work
 * pointer to UnitSprite_SetScl; rand() % 360 is evaluated twice per flash angle (Flash, FlashInit); FlashInit
 * zero-fills an unused local Scl[10]. */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU rodata 0x1ee03d0 (nodwarf.h): read by the hle-hooked Movie_PlayForMenu, must be PS2 memory in the hybrid */
char mtitle_movie_path[33] = "cdrom0:\\MOVIE\\DISC\\ACNB_OP.PSS;1";
#endif

/* gTitleLayoutName (0x1ee0340): UnitSprite_New reads the name only during the call, host literals are fine */
static char *const mtitle_layout_name[10] = {
    (char *)"BG",    (char *)"flash", (char *)"logokage", (char *)"logo",  (char *)"start",
    (char *)"menu0", (char *)"menu1", (char *)"menu2",    (char *)"menu3", (char *)"copyright",
};

/* obj+0x10: inline title work / flash work */
#define MT_W(o) ((struct struct_2749aef8 *)&(o)->dt)
#define MT_F(o) ((struct struct_3e82413b *)&(o)->dt)

#define MT_LAYOUT() ((struct _LO_HEADER *)LAYOUT_GetLayoutData(0x457))

static const float MT_SCL_STEP = 0x1.47ae14p-6f;  /* 0.02f 0x3ca3d70a */
static const float MT_SCL_MIN = 0x1.99999ap-1f;   /* 0.8f 0x3f4ccccd */
static const float MT_SIXTH = 0x1.555556p-3f;     /* 1/6 0x3e2aaaab */
static const float MT_PI = 0x1.921fb6p+1f;        /* 0x40490fdb */
static const float MT_2PI = 0x1.921fb6p+2f;       /* 0x40c90fdb */

/* MWCC float -> unsigned idiom (>= 2^31: subtract, cvt.w.s, or the top bit), stored with sb */
static inline unsigned char mt_f2u8(float f)
{
    if (f >= 2147483648.0f) return (unsigned char)(ps2_ftoi(f - 2147483648.0f) | (int)0x80000000);
    return (unsigned char)ps2_ftoi(f);
}

/* flash alpha from the scale: rises to 64 at Scl 4, falls to 0 at 8 (cvt.w.s, sb) */
static inline unsigned char mt_scl_alpha(float scl)
{
    float t = scl / 8.0f;
    if (t <= 0.5f) {
        float a = 128.0f * scl;
        a = a / 8.0f;
        return (unsigned char)ps2_ftoi(a);
    }
    float a = 1.0f - t;
    a = 128.0f * a;
    return (unsigned char)ps2_ftoi(a);
}

/* a random flash angle: rand() % 360 evaluated twice (original macro double evaluation, quirk); r1 only decides
 * whether 2pi is subtracted */
static inline float mt_rand_ang(void)
{
    int r1 = ps2_rand() % 360;
    float x = MT_PI * (float)r1;
    x = x / 180.0f;
    x = MT_PI + x;
    x = x / MT_2PI;
    float f20 = (float)ps2_ftoi(x);
    f20 = MT_2PI * f20;
    int r2 = ps2_rand() % 360;
    float y = MT_PI * (float)r2;
    y = y / 180.0f;
    y = MT_PI + y;
    y = y - f20;
    return y - MT_PI;
}

static inline void mt_erase(struct stMobj *o)
{
    o->f_draw = 0;
    o->f_erase = 1;
}

/* 0x1e9b150: hide the ten flash sprites */
static void __MenuTitle_FlashHide(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_3e82413b *pWork = MT_F(pMobj);
    for (int i = 0; i < 10; i++)
        if (pWork->flash[i] != 0) DF_MOBJ_SetDrawEnable(pWork->flash[i], 0);
}

/* 0x1e9b1b0: 7-frame fade of the flash sprites (alpha * (1 - timer / 6)) */
static void __MenuTitle_FlashFadeOut(struct stMobj *pMobj)
{
    PS2_FRAME(0x70);
    struct struct_3e82413b *pWork = MT_F(pMobj);
    struct struct_76b810ec col = {0x80, 0x80, 0x80, 0x80};   /* gp-0x6db8 */
    pWork->timer++;
    for (int i = 0; i < 10; i++) {
        float s = pWork->Scl[i] + MT_SCL_STEP;
        pWork->Scl[i] = s;
        if (!(s < 8.0f)) pWork->Scl[i] = MT_SCL_MIN;
        col.a = mt_scl_alpha(pWork->Scl[i]);
        float f1 = MT_SIXTH * (float)pWork->timer;
        f1 = 1.0f - f1;
        col.a = mt_f2u8((float)col.a * f1);
        UnitSprite_SetMultiply(pWork->flash[i], &col);
        UnitSprite_SetScl((struct stMobj *)pWork, pWork->Scl[i]);   /* original passes the work pointer (quirk) */
    }
    unsigned char old = pMobj->count;
    pMobj->count = old + 1;
    if (old < 6) return;
    col.a = mt_f2u8((float)col.a * 0.5f);   /* dead store */
    pMobj->play_func = (void (*)(void))GFN(__MenuTitle_FlashHide);
    pWork->timer = 0;
}

/* 0x1e9b440: flash sprites grow 0.02 per frame, restart at 0.8 with a new random angle */
static void __MenuTitle_Flash(struct stMobj *pMobj)
{
    PS2_FRAME(0x70);
    struct struct_3e82413b *pWork = MT_F(pMobj);
    struct struct_76b810ec col = {0x80, 0x80, 0x80, 0x80};   /* gp-0x6dbc */
    for (int i = 0; i < 10; i++) {
        float s = pWork->Scl[i] + MT_SCL_STEP;
        pWork->Scl[i] = s;
        if (!(s < 8.0f)) {
            pWork->Scl[i] = MT_SCL_MIN;
            float ang = mt_rand_ang();
            UnitSprite_SetAng(pWork->flash[i], ang);
        }
        col.a = mt_scl_alpha(pWork->Scl[i]);
        col.a = mt_f2u8((float)col.a * 0.5f);
        UnitSprite_SetMultiply(pWork->flash[i], &col);
        UnitSprite_SetScl(pWork->flash[i], pWork->Scl[i]);
    }
    pWork->timer = 0;
}

/* 0x1e9b6b0: 7-frame start of the flash sprites, then Flash */
static void __MenuTitle_FlashFadeIn(struct stMobj *pMobj)
{
    PS2_FRAME(0x70);
    struct struct_3e82413b *pWork = MT_F(pMobj);
    struct struct_76b810ec col = {0x80, 0x80, 0x80, 0x80};   /* gp-0x6dc0 */
    pWork->timer++;
    for (int i = 0; i < 10; i++) {
        DF_MOBJ_SetDrawEnable(pWork->flash[i], 1);
        float s = pWork->Scl[i] + MT_SCL_STEP;
        pWork->Scl[i] = s;
        if (!(s < 8.0f)) pWork->Scl[i] = MT_SCL_MIN;
        col.a = mt_scl_alpha(pWork->Scl[i]);
        col.a = mt_f2u8((float)col.a * 0.5f);
        UnitSprite_SetMultiply(pWork->flash[i], &col);
        UnitSprite_SetScl(pWork->flash[i], pWork->Scl[i]);
    }
    unsigned char old = pMobj->count;
    pMobj->count = old + 1;
    if (old < 6) return;
    pMobj->play_func = (void (*)(void))GFN(__MenuTitle_Flash);
    pWork->timer = 0;
}

/* 0x1e9b8b0: the flash object: ten "flash" sprites (children of pMobj) with staggered scales and random angles */
static struct stMobj *__MenuTitle_FlashInit(struct stMobj *pMobj)
{
    PS2_FRAME(0x90);
    PS2_STACK(unsigned char[0x28], Scl_unused, 0x60);   /* DWARF local float Scl[10], zero-filled, never read */
    memset(Scl_unused, 0, 0x28);
    struct stMobj *pNewObj = DF_MOBJ_GetNewObject(0);
    if (pNewObj == 0) return 0;
    struct struct_3e82413b *pWork = MT_F(pNewObj);
    for (int i = 0; i < 10; i++) {
        float m = MT_SCL_MIN * (float)i;
        pWork->Scl[i] = 8.0f - m;
    }
    for (int i = 0; i < 10; i++) {
        struct _LO_HEADER *lo = MT_LAYOUT();
        pWork->flash[i] = UnitSprite_New(pMobj, lo, mtitle_layout_name[1]);
        pWork->flash[i]->draw_func = (void (*)(void))GFN(__UnitSprite_DrawSclAng);
        float ang = mt_rand_ang();
        UnitSprite_SetScl(pWork->flash[i], pWork->Scl[i]);
        UnitSprite_SetAng(pWork->flash[i], ang);
    }
    for (int i = 0; i < 10; i++)
        if (pWork->flash[i] != 0) DF_MOBJ_SetDrawEnable(pWork->flash[i], 0);
    pNewObj->play_func = (void (*)(void))GFN(__MenuTitle_FlashFadeIn);
    pNewObj->count = 0;
    return pNewObj;
}

/* 0x1e9bb20: play the opening movie (hle stub: skipped, returns FINISH) */
static void __MenuTitle_SetPlayMovie(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    int size = fsmpg_GetRequiredBufferSize(2, 0);
    char *buf = (char *)DF_MMM_Alloc(0, size + 0x3f);
    pMobj->stat = 6;
    pWork->isReEnterMovie = 0;
    if (buf == 0) return;
    char *path = Env_ConvertPath(mtitle_movie_path);
    /* 64-byte align via signed division of the 32-bit address (sra 6 idiom), as an offset so the source port keeps
     * its full host pointer */
    int a = (int)(PS2PTR(buf) + 0x3f);
    char *aligned = buf + ((a / 64) * 64 - (int)PS2PTR(buf));
    eMovieResult r = Movie_PlayForMenu(path, aligned, 1, 0, 0, 0, 0);
    if (r == 2) {
        pWork->isSkipMovie = 1;
        pMobj->play_func = (void (*)(void))GFN(__MenuTitleInit);
    } else {
        pMobj->play_func = (void (*)(void))GFN(__MenuTitleInitNoFadeLogo);
    }
    if (buf != 0) DF_MMM_Free(0, buf);
}

/* 0x1e9bc10: after 6 frames of fade-out delete the title sprites and textures, erase the object */
static void __MenuTitle_PlayPhaseCancel(struct stMobj *pMobj)
{
    PS2_FRAME(0x60);
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    unsigned char old = pMobj->count;
    pMobj->count = old + 1;
    if (old < 6) return;
    UnitSprite_Delete(pWork->BG);
    struct stMobj *flashobj = pWork->flash;
    for (int i = 0; i < 10; i++) UnitSprite_Delete(MT_F(flashobj)->flash[i]);   /* no NULL test (original) */
    if (flashobj != 0) mt_erase(flashobj);
    UnitSprite_Delete(pWork->logokage);
    UnitSprite_Delete(pWork->logo);
    UnitSprite_Delete(pWork->copyright);
    UnitSprite_Delete(pWork->menu0);
    UnitSprite_Delete(pWork->menu1);
    UnitSprite_Delete(pWork->menu2);
    UnitSprite_Delete(pWork->menu3);
    UnitSprite_Delete(pWork->start);
    pMobj->stat = 5;
    pMobj->play_func = 0;
    pMobj->draw_func = 0;
    pMobj->pkt_func = 0;
    for (int k = 0; k < 2; k++) {   /* dead: the logo states clear sprtp[] */
        if (pWork->sprtp[k] != 0) {
            mt_erase(pWork->sprtp[k]);
            pWork->sprtp[k] = 0;
        }
    }
    MenuTex_UnRegisterBndTexture(pWork->FSLogo);
    if (pWork->FSLogo != 0) {
        DF_MMM_Free(0, pWork->FSLogo);
        pWork->FSLogo = 0;
    }
    MenuTex_UnRegisterBndTexture(pWork->pTM2);
    if (pWork->pTM2 != 0) {
        DF_MMM_Free(0, pWork->pTM2);
        pWork->pTM2 = 0;
    }
    if (pMobj != 0) mt_erase(pMobj);
}

/* 0x1e9be40: menu decided: fade everything out, flash object to FlashFadeOut */
static void __MenuTitle_InitPhaseCancel(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    pWork->phase = 3;
    pMobj->count = 0;
    pMobj->play_func = (void (*)(void))GFN(__MenuTitle_PlayPhaseCancel);
    UnitSprite_SetFadeOut(pWork->menu0);
    UnitSprite_SetFadeOut(pWork->menu1);
    UnitSprite_SetFadeOut(pWork->menu2);
    UnitSprite_SetFadeOut(pWork->menu3);
    UnitSprite_SetFadeOut(pWork->BG);
    pWork->flash->play_func = (void (*)(void))GFN(__MenuTitle_FlashFadeOut);
    UnitSprite_SetFadeOut(pWork->logokage);
    UnitSprite_SetFadeOut(pWork->logo);
    UnitSprite_SetFadeOut(pWork->copyright);
}

/* 0x1e9bed0: the 4-row menu: Cross decides, Circle back to PRESS START, Up / Down move (wrap) */
static void __MenuTitle_MoveCursor(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    struct struct_76b810ec col[4];
    for (int i = 0; i < 4; i++) {
        col[i].r = 0x80;
        col[i].g = 0x80;
        col[i].b = 0x80;
        col[i].a = 0x40;
    }
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    if (pad->Rdw == 1) {
        switch (pWork->cursor) {
        case 0: MenuMng.ret = 9; break;
        case 1: MenuMng.ret = 0xa; break;
        case 2: MenuMng.ret = 2; break;
        case 3: MenuMng.ret = 0x15; break;
        }
        pMobj->play_func = (void (*)(void))GFN(__MenuTitle_InitPhaseCancel);
        Voice_Call(0x3ea, 0x7f);
    } else if (pad->Rrt == 1) {
        UnitSprite_SetFadeIn(pWork->start);
        UnitSprite_SetFadeOut(pWork->menu0);
        UnitSprite_SetFadeOut(pWork->menu1);
        UnitSprite_SetFadeOut(pWork->menu2);
        UnitSprite_SetFadeOut(pWork->menu3);
        pWork->step--;
        pMobj->play_func = (void (*)(void))GFN(__MenuTitle_WaitStartButton);
    } else {
        if (pad->up == 1) {
            pWork->cursor--;
            Voice_Call(0x3e9, 0x7f);
        } else if (pad->dw == 1) {
            pWork->cursor++;
            Voice_Call(0x3e9, 0x7f);
        }
        pWork->cursor = (pWork->cursor + 4) & 3;
        col[pWork->cursor].a = 0x80;
    }
    UnitSprite_SetMultiply(pWork->menu0, &col[0]);
    UnitSprite_SetMultiply(pWork->menu1, &col[1]);
    UnitSprite_SetMultiply(pWork->menu2, &col[2]);
    UnitSprite_SetMultiply(pWork->menu3, &col[3]);
}

/* 0x1e9c110: attract tear-down on the copied work (new object made by WaitStartButton) */
static void __MenuTitle_StartFadeOut(struct stMobj *pMobj)
{
    PS2_FRAME(0x60);
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    unsigned char old = pMobj->count;
    pMobj->count = old + 1;
    if (old < 6) return;
    UnitSprite_Delete(pWork->BG);
    struct stMobj *flashobj = pWork->flash;
    for (int i = 0; i < 10; i++) UnitSprite_Delete(MT_F(flashobj)->flash[i]);   /* no NULL test (original) */
    if (flashobj != 0) mt_erase(flashobj);
    UnitSprite_Delete(pWork->logokage);
    UnitSprite_Delete(pWork->logo);
    UnitSprite_Delete(pWork->copyright);
    UnitSprite_Delete(pWork->menu0);
    UnitSprite_Delete(pWork->menu1);
    UnitSprite_Delete(pWork->menu2);
    UnitSprite_Delete(pWork->menu3);
    if (pMobj != 0) mt_erase(pMobj);
}

/* 0x1e9c250: PRESS START; after 900 idle frames hand the scene to a StartFadeOut copy and restart the logos */
static void __MenuTitle_WaitStartButton(struct stMobj *pMobj)
{
    PS2_FRAME(0x40);
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    if (MPad[MPadUser].start == 1) {
        UnitSprite_SetFadeOut(pWork->start);
        DF_MOBJ_SetDrawEnable(pWork->menu0, 1);
        DF_MOBJ_SetDrawEnable(pWork->menu1, 1);
        DF_MOBJ_SetDrawEnable(pWork->menu2, 1);
        DF_MOBJ_SetDrawEnable(pWork->menu3, 1);
        UnitSprite_SetFadeIn(pWork->menu0);
        UnitSprite_SetFadeIn(pWork->menu1);
        UnitSprite_SetFadeIn(pWork->menu2);
        UnitSprite_SetFadeIn(pWork->menu3);
        pMobj->play_func = (void (*)(void))GFN(__MenuTitle_MoveCursor);
        pWork->cursor = Mtitle_snum != 0 ? 1 : 0;
        Voice_Call(0x3ea, 0x7f);
        pWork->step++;
    }
    if (pWork->cntInside >= 0x384) {
        UnitSprite_SetFadeOut(pWork->BG);
        UnitSprite_SetFadeOut(pWork->logokage);
        UnitSprite_SetFadeOut(pWork->logo);
        UnitSprite_SetFadeOut(pWork->copyright);
        UnitSprite_Delete(pWork->start);
        pWork->flash->play_func = (void (*)(void))GFN(__MenuTitle_FlashFadeOut);
        pMobj->play_func = (void (*)(void))GFN(__MenuTitle_USLogoInit);
        pMobj->count = 0;
        struct stMobj *pNew = DF_MOBJ_GetNewObject(0);
        if (pNew == 0) return;
        FlushCache(0);
        pNew->play_func = (void (*)(void))GFN(__MenuTitle_StartFadeOut);
        memcpy(&pNew->dt, pWork, 0xa4);
        pWork->BG = 0;
        pWork->flash = 0;
        pWork->logokage = 0;
        pWork->logo = 0;
        pWork->copyright = 0;
        pWork->menu0 = 0;
        pWork->menu1 = 0;
        pWork->menu2 = 0;
        pWork->menu3 = 0;
        pNew->count = 0;
    }
    pWork->cntInside++;
}

/* 0x1e9c410: at timer 30 create the blinking PRESS START sprite */
static void __MenuTitle_FadeInPlay(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    if (pWork->timer == 30) {
        struct _LO_HEADER *lo = MT_LAYOUT();
        pWork->start = UnitSpriteBlink_New(pMobj, lo, mtitle_layout_name[4]);
        UnitSprite_SetFadeIn(pWork->start);
        pWork->step++;
        pMobj->play_func = (void (*)(void))GFN(__MenuTitle_WaitStartButton);
        pWork->timer = 0;
        pWork->start->draw_prio = 3;
    }
    if (pWork->timer < 31) pWork->timer++;
}

/* the sprites of the title scene (InitNoFadeLogo / Init, same creation order) */
static inline void mt_create_scene(struct stMobj *pMobj, struct struct_2749aef8 *pWork)
{
    struct _LO_HEADER *lo = MT_LAYOUT();
    pWork->BG = UnitSprite_New(pMobj, lo, mtitle_layout_name[0]);
    pWork->flash = __MenuTitle_FlashInit(pMobj);
    lo = MT_LAYOUT();
    pWork->logokage = UnitSprite_New(pMobj, lo, mtitle_layout_name[2]);
    lo = MT_LAYOUT();
    pWork->logo = UnitSprite_New(pMobj, lo, mtitle_layout_name[3]);
    lo = MT_LAYOUT();
    pWork->copyright = UnitSprite_New(pMobj, lo, mtitle_layout_name[9]);
    lo = MT_LAYOUT();
    pWork->menu0 = UnitSprite_New(pMobj, lo, mtitle_layout_name[5]);
    lo = MT_LAYOUT();
    pWork->menu1 = UnitSprite_New(pMobj, lo, mtitle_layout_name[6]);
    lo = MT_LAYOUT();
    pWork->menu2 = UnitSprite_New(pMobj, lo, mtitle_layout_name[7]);
    lo = MT_LAYOUT();
    pWork->menu3 = UnitSprite_New(pMobj, lo, mtitle_layout_name[8]);
}

static inline void mt_hide_menu(struct struct_2749aef8 *pWork)
{
    DF_MOBJ_SetDrawEnable(pWork->menu0, 0);
    DF_MOBJ_SetDrawEnable(pWork->menu1, 0);
    DF_MOBJ_SetDrawEnable(pWork->menu2, 0);
    DF_MOBJ_SetDrawEnable(pWork->menu3, 0);
}

/* 0x1e9c4d0: build the title scene (movie played to the end); the logo itself is not faded in */
static void __MenuTitleInitNoFadeLogo(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    pWork->timer = 0;
    pWork->step = 0;
    pWork->cntInside = 0;
    pWork->stepInside = 0;
    pWork->perform[5] = 1.0f;
    mt_create_scene(pMobj, pWork);
    UnitSprite_SetFadeIn(pWork->BG);
    UnitSprite_SetFadeIn(pWork->logokage);
    UnitSprite_SetFadeIn(pWork->copyright);
    mt_hide_menu(pWork);
    pMobj->play_func = (void (*)(void))GFN(__MenuTitle_FadeInPlay);
}

/* 0x1e9c680: movie skipped: wait 31 frames, then build the title scene with the logo faded in too */
static void __MenuTitleInit(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    if (pWork->timer < 31) {
        pWork->timer++;
        return;
    }
    pWork->timer = 0;
    pWork->step = 0;
    pWork->cntInside = 0;
    pWork->stepInside = 0;
    pWork->perform[5] = 1.0f;
    mt_create_scene(pMobj, pWork);
    UnitSprite_SetFadeIn(pWork->BG);
    UnitSprite_SetFadeIn(pWork->logokage);
    UnitSprite_SetFadeIn(pWork->logo);
    UnitSprite_SetFadeIn(pWork->copyright);
    mt_hide_menu(pWork);
    pMobj->play_func = (void (*)(void))GFN(__MenuTitle_FadeInPlay);
}

/* ---- g2: the US / FS logo states and the entry ---- */

/* logo alpha (timer << 7) / 30 (u32: mulhi 0x88888889, srl 4) */
static inline unsigned int mt_logo_alpha(unsigned int timer)
{
    return (timer << 7) / 30u;
}

/* Start, Circle or Cross edge (tested in that order) */
static inline int mt_logo_skip(void)
{
    struct struct_2d84b07b *pad = &MPad[MPadUser];
    return pad->start == 1 || pad->Rrt == 1 || pad->Rdw == 1;
}

#define MT_ALPHA(o) ((o)->dt.sprite.cur_col.a)   /* NX sprite cur_col.a, obj+0x53 */

/* 0x1e9c850: 30-frame fade-out of the US logo, then the FS logo */
static void __MenuTitle_USLogoFadeOut(struct stMobj *pMobj)
{
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    unsigned int old = pWork->timer;
    pWork->timer = old + 1;
    if (old < 30) {
        MT_ALPHA(pWork->sprtp[0]) = (unsigned char)(0x80 - mt_logo_alpha(pWork->timer));
        return;
    }
    pWork->timer = 0;
    pMobj->play_func = (void (*)(void))GFN(__MenuTitle_FSLogoInit);
    pMobj->stat = 1;
    if (pWork->sprtp[0] != 0) {
        mt_erase(pWork->sprtp[0]);
        pWork->sprtp[0] = 0;
    }
}

/* 0x1e9c910: 30-frame fade-out of the two FS logo halves, then the opening movie (stat unchanged) */
static void __MenuTitle_FSLogoFadeOut(struct stMobj *pMobj)
{
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    unsigned int old = pWork->timer;
    pWork->timer = old + 1;
    if (old < 30) {
        unsigned char a = (unsigned char)(0x80 - mt_logo_alpha(pWork->timer));
        MT_ALPHA(pWork->sprtp[0]) = a;
        MT_ALPHA(pWork->sprtp[1]) = a;
        return;
    }
    pWork->timer = 0;
    pMobj->play_func = (void (*)(void))GFN(__MenuTitle_SetPlayMovie);
    for (int k = 0; k < 2; k++) {
        if (pWork->sprtp[k] != 0) {
            mt_erase(pWork->sprtp[k]);
            pWork->sprtp[k] = 0;
        }
    }
}

/* 0x1e9ca10: hold the US logo 90 frames (skippable) */
static void __MenuTitle_USLogoPlay(struct stMobj *pMobj)
{
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    unsigned int old = pWork->timer;
    pWork->timer = old + 1;
    if (old >= 90 || mt_logo_skip()) {
        pMobj->stat = 2;
        pWork->timer = 0;
        pMobj->play_func = (void (*)(void))GFN(__MenuTitle_USLogoFadeOut);
    }
}

/* 0x1e9caa0: hold the FS logo 90 frames (skippable) */
static void __MenuTitle_FSLogoPlay(struct stMobj *pMobj)
{
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    unsigned int old = pWork->timer;
    pWork->timer = old + 1;
    if (old >= 90 || mt_logo_skip()) {
        pMobj->stat = 2;
        pWork->timer = 0;
        pMobj->play_func = (void (*)(void))GFN(__MenuTitle_FSLogoFadeOut);
    }
}

/* 0x1e9cb30: 30-frame fade-in of the US logo. The skip arm does not reset timer (FadeOut starts from the fade-in
 * count) and falls through: on the frame the fade ends the Play state overrides the skip (original). */
static void __MenuTitle_USLogoFadeIn(struct stMobj *pMobj)
{
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    if (mt_logo_skip()) {
        pMobj->stat = 2;
        pMobj->play_func = (void (*)(void))GFN(__MenuTitle_USLogoFadeOut);
    }
    unsigned int old = pWork->timer;
    pWork->timer = old + 1;
    unsigned int a;
    if (old < 30) {
        a = mt_logo_alpha(pWork->timer);
    } else {
        pWork->timer = 0;
        a = 0x80;
        pMobj->play_func = (void (*)(void))GFN(__MenuTitle_USLogoPlay);
    }
    MT_ALPHA(pWork->sprtp[0]) = (unsigned char)a;
}

/* 0x1e9cc10: 30-frame fade-in of the FS logo (same as USLogoFadeIn, two sprites) */
static void __MenuTitle_FSLogoFadeIn(struct stMobj *pMobj)
{
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    if (mt_logo_skip()) {
        pMobj->stat = 2;
        pMobj->play_func = (void (*)(void))GFN(__MenuTitle_FSLogoFadeOut);
    }
    unsigned int old = pWork->timer;
    pWork->timer = old + 1;
    unsigned int a;
    if (old < 30) {
        a = mt_logo_alpha(pWork->timer);
    } else {
        pWork->timer = 0;
        a = 0x80;
        pMobj->play_func = (void (*)(void))GFN(__MenuTitle_FSLogoPlay);
    }
    MT_ALPHA(pWork->sprtp[0]) = (unsigned char)a;
    MT_ALPHA(pWork->sprtp[1]) = (unsigned char)a;
}

/* 0x1e9ccf0: the full-screen US logo sprite (tex 0xdae), alpha 0 */
static void __MenuTitle_USLogoInit(struct stMobj *pMobj)
{
    PS2_FRAME(0x60);
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    /* 0x1ee03b0 / 0x1ee03c0, 16-aligned like the original sp+0x50 / sp+0x40 (lq copies) */
    struct stMenuRect rect __attribute__((aligned(16))) = {0, 0, 0x280, 0x1c0};
    int uv[4] __attribute__((aligned(16))) = {0, 0, 0x280, 0x1c0};
    pWork->sprtp[0] = DF_MOBJ_NXNewSprite(pMobj, 0xdae, &rect, uv, 0, 1, 0, 0, 0);
    MT_ALPHA(pWork->sprtp[0]) = 0;
    pWork->timer = 0;
    pMobj->play_func = (void (*)(void))GFN(__MenuTitle_USLogoFadeIn);
}

/* 0x1e9cd80: the FS logo as two sprites (tex 0xdac 512 wide + 0xdad 128 wide), alpha 0 */
static void __MenuTitle_FSLogoInit(struct stMobj *pMobj)
{
    PS2_FRAME(0xc0);
    struct struct_2749aef8 *pWork = MT_W(pMobj);
    /* 0x1ee0370 / 0x1ee0390, 16-aligned like the original sp+0xa0 / sp+0x80 */
    struct stMenuRect rect[2] __attribute__((aligned(16))) = {{0, 0, 0x200, 0x1c0}, {0x200, 0, 0x80, 0x1c0}};
    int uv[2][4] __attribute__((aligned(16))) = {{0, 0, 0x200, 0x1c0}, {0, 0, 0x80, 0x1c0}};
    for (int i = 0; i < 2; i++) {
        pWork->sprtp[i] = DF_MOBJ_NXNewSprite(pMobj, 0xdac + i, &rect[i], uv[i], 0, 1, 0, 0, 0);
        MT_ALPHA(pWork->sprtp[i]) = 0;
    }
    pWork->timer = 0;
    pMobj->play_func = (void (*)(void))GFN(__MenuTitle_FSLogoFadeIn);
}

/* 0x1e9ce70: the title object (called by mmanage): resets the game data, loads the logo / title texture files */
struct stMobj *MTitle_NewSystem(void)
{
    PS2_FRAME(0x30);
    struct stMobj *pNew = DF_MOBJ_GetNewObject(0);
    if (pNew == 0) return 0;
    struct struct_2749aef8 *pWork = MT_W(pNew);
    memset(pWork, 0, 0xa4);
    MenuMng.f_new_game = 1;
    Game_InitAllData();
    DF_MVIEW_Init();
    DF_InitMemCard();
    DF_MBACK_SetDraw(0);
    pWork->FSLogo = (unsigned char *)DF_MMM_Alloc(0, File_GetSize(0xbbb));
    pWork->pTM2 = (unsigned char *)DF_MMM_Alloc(0, File_GetSize(0x1009));
    File_Read(0xbbb, -1, (char *)pWork->FSLogo, 1);
    File_Read(0x1009, -1, (char *)pWork->pTM2, 1);
    MenuTex_RegisterBndTexture(pWork->FSLogo);
    MenuTex_RegisterBndTexture(pWork->pTM2);
    DF_MENU_SetFadeIn(0);
    DF_MBGUIDE_Hide();
    pNew->type = 0;
    pNew->stat = 1;
    pNew->count = 0;
    pNew->play_func = (void (*)(void))GFN(__MenuTitle_USLogoInit);
    pNew->draw_func = 0;
    pNew->pkt_func = 0;
    return pNew;
}
