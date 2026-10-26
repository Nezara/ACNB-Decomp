/* src/menu/munit_colorbar.cpp - the colour bar of the Training / mission-select screens.
 * One stMobj whose MOBJManager_Create (layout 0x443, 3-entry table) builds two sprites "colorbar_mono" /
 * "colorbar_grad" and the caption string "string_training". FadeInStart / FadeOutStart set the slide phase, Play
 * slides the group and the two sprite child objects by +-0x130 / 0x260 px over 8 frames (MoveMng mode 1) and shows
 * the caption at the end; ChangeCursor re-colours both sprites from a 6-entry RGB table and sets the caption id.
 * DrawSprite is the sprite children's own draw function (installed by Fade*Start).
 * The table's pLayoutHeader is rewritten before every MOBJManager_* call, so the hybrid keeps it at its PS2 address
 * (nodwarf munit_colorbar_mng). */
#include <string.h>
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
/* OLMENU 0x1ee16e0..0x1ee17cf */
char munit_colorbar_name_mono[14] = "colorbar_mono";
char munit_colorbar_name_grad[14] = "colorbar_grad";
static char munit_colorbar_name_training[] = "string_training";
struct stMobjManager munit_colorbar_mng[3] = {
    {.pLayoutName = munit_colorbar_name_mono, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_colorbar_name_grad, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 1},
    {.pLayoutName = munit_colorbar_name_training, .pLayoutHeader = 0, .numContinue = 1, .typeMObj = 2,
     .flag = 0, .dummy_c0 = {0}, .exString = {.category = 0, .strid = 0xdbcb8}},
};
#endif

#define MUCB_MNG munit_colorbar_mng
#define MUCB_END (-99999)   /* MoveMng_GetNextMoveValue: move finished */

/* the mobj's dt (no DWARF struct; NewSystem clears 0x24 bytes) */
struct munit_colorbar_work {
    int unused0;                    /* 0x0 never touched */
    struct stMobj *apMobj[3];       /* 0x4 [0] colorbar_mono, [1] colorbar_grad, [2] string_training */
    signed char phase;              /* 0x10 */
    signed char pad[3];             /* 0x11 */
    struct struct_d46b729f mng;     /* 0x14 */
};
#define CB(o) ((struct munit_colorbar_work *)&(o)->dt)

/* RGB per cursor index (OLMENU 0x1ee17d0, copied to the stack with lq / lh) */
static const unsigned char mucb_tab[18] = {
    0xb6, 0x45, 0x13, 0xab, 0x9a, 0x1c, 0x1a, 0x80, 0x42, 0x0c, 0x6f, 0x83, 0x88, 0x0c, 0xa3, 0x30, 0x33, 0x9f,
};
/* DrawSprite's initial corner colours (OLMENU 0x1ee17f0) */
static const unsigned char mucb_col_init[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0x42, 0x2c, 0x23, 0, 0x42, 0x2c, 0x23, 0};

static inline void mucb_set_header(void)
{
    MUCB_MNG[0].pLayoutHeader = (struct _LO_HEADER *)LAYOUT_GetLayoutData(0x443);
}

/* move the managed group and both sprite child objects by dx */
static inline void mucb_move(struct munit_colorbar_work *d, int dx)
{
    mucb_set_header();
    MOBJManager_Move(MUCB_MNG, 3, &d->apMobj[0], dx, 0, 0);
    UnitSprite_GetChildObject(d->apMobj[0], 0)->dt.sprite.rect.x += dx;
    UnitSprite_GetChildObject(d->apMobj[1], 0)->dt.sprite.rect.x += dx;
}

/* back to the layout position; the sprite children's x / y come from the layout's part data */
static void __MUnitColorbar_Reset(struct stMobj *pMobj)
{
    PS2_FRAME(0xa0);
    PS2_STACK(char[32], name, 0x30);
    PS2_STACK(struct _LAYOUT_RET_DATA, out, 0x50);
    struct munit_colorbar_work *d = CB(pMobj);

    mucb_set_header();
    MOBJManager_ResetPos(MUCB_MNG, 3, &d->apMobj[0]);
    ps2_sprintf_d(name, munit_colorbar_name_mono, 0);   /* no conversion in the format */
    if (!DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x443), (unsigned char *)name, 0, &out)) return;
    struct stMobj *c = UnitSprite_GetChildObject(d->apMobj[0], 0);
    c->dt.sprite.rect.x = out.Pos.x;
    c->dt.sprite.rect.y = out.Pos.y;
    ps2_sprintf_d(name, munit_colorbar_name_grad, 0);
    if (!DF_LO_GetLayoutData((unsigned char *)LAYOUT_GetLayoutData(0x443), (unsigned char *)name, 0, &out)) return;
    c = UnitSprite_GetChildObject(d->apMobj[1], 0);
    c->dt.sprite.rect.x = out.Pos.x;
    c->dt.sprite.rect.y = out.Pos.y;
}

/* draw function of the two sprite child objects: corners 0 / 1 in the sprite's colour, corners 2 / 3 too when
 * texid == -1 (set by CreateItem for colorbar_mono only), else the fixed dark colour; all alphas = cur_col.a */
static void __MUnitColorbar_DrawSprite(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    PS2_STACK(struct struct_76b810ec[4], col, 0x20);
    struct struct_37ce77ad *s = &pMobj->dt.sprite;

    memcpy(col, mucb_col_init, 16);
    col[2].a = s->cur_col.a;
    col[3].a = s->cur_col.a;
    memcpy(&col[0], &s->cur_col, 4);
    memcpy(&col[1], &s->cur_col, 4);
    if (s->texid == -1) {
        memcpy(&col[2], &s->cur_col, 4);
        memcpy(&col[3], &s->cur_col, 4);
    }
    unsigned char a = s->cur_col.a;
    col[3].a = a;
    col[2].a = a;
    col[1].a = a;
    col[0].a = a;
    DF_MDRAW_KickReg(0x42, 0x44);
    DF_MDRAW_DrawTexSprite4ColorsI(0x514, &s->rect, s->uv, col, 1);
}

/* no bounds check on indexNew (0..5) */
static void __MUnitColorbar_ChangeCursor(struct stMobj *pMobj, int indexNew)
{
    PS2_FRAME(0x70);
    PS2_STACK(unsigned char[18], tab, 0x50);
    struct munit_colorbar_work *d = CB(pMobj);

    memcpy(tab, mucb_tab, 18);
    d->apMobj[2]->dt.string.id = 0xdbbb4 + indexNew;
    const unsigned char *rgb = &tab[indexNew * 3];
    for (int k = 0; k < 2; k++) {
        struct struct_37ce77ad *s = &UnitSprite_GetChildObject(d->apMobj[k], 0)->dt.sprite;
        s->init_col.r = s->next_col.r = s->prev_col.r = s->cur_col.r = rgb[0];
        s->init_col.g = s->next_col.g = s->prev_col.g = s->cur_col.g = rgb[1];
        s->init_col.b = s->next_col.b = s->prev_col.b = s->cur_col.b = rgb[2];
    }
}

static void __MUnitColorbar_CreateItem(struct stMobj *pMobj)
{
    PS2_FRAME(0x30);
    struct munit_colorbar_work *d = CB(pMobj);

    mucb_set_header();
    MOBJManager_Create(pMobj, &d->apMobj[0], 3, MUCB_MNG);
    d->apMobj[2]->dt.string.flag |= 0x800;
    struct stMobj *c = UnitSprite_GetChildObject(d->apMobj[0], 0);
    c->dt.sprite.texp = 0;
    c->dt.sprite.texid = -1;
    c->draw_func = 0;
    c->pkt_func = 0;
    d->apMobj[0]->draw_func = 0;
    c = UnitSprite_GetChildObject(d->apMobj[1], 0);
    c->draw_func = 0;
    c->pkt_func = 0;
    d->apMobj[1]->draw_func = 0;
}

static void __MUnitColorbar_Pkt(void)
{
}

static void __MUnitColorbar_Draw(void)
{
}

/* slide step: at the end the phase goes back to 0 (and the caption fades in when `caption`) */
static inline void mucb_slide(struct munit_colorbar_work *d, int dx, int caption)
{
    int v = MoveMng_GetNextMoveValue(&d->mng, dx, 8, 1);
    if (v == MUCB_END) {
        if (caption) {
            DF_MOBJ_SetDrawEnable(d->apMobj[2], 1);
            DF_MOBJ_SetFadeIn(d->apMobj[2]);
        }
        d->phase = 0;
    } else {
        mucb_move(d, v);
    }
}

/* a chain of independent `if (phase == X)` blocks, each re-reading phase: a setup block falls through into its
 * slide block in the same call */
static void __MUnitColorbar_Play(struct stMobj *pMobj)
{
    PS2_FRAME(0x50);
    struct munit_colorbar_work *d = CB(pMobj);

    if (d->phase == 1) {
        __MUnitColorbar_Reset(pMobj);
        mucb_move(d, 0x130);
        d->phase = 2;
    }
    if (d->phase == 2) mucb_slide(d, -0x130, 1);
    if (d->phase == 3) {
        __MUnitColorbar_Reset(pMobj);
        mucb_move(d, 0x260);
        d->phase = 4;
    }
    if (d->phase == 4) mucb_slide(d, -0x130, 1);
    if (d->phase == 0xd) {
        mucb_set_header();
        MOBJManager_FadeOut(&d->apMobj[0], 0, 3, MUCB_MNG);
        __MUnitColorbar_Reset(pMobj);
        d->phase = 0xe;
    }
    if (d->phase == 0xe) mucb_slide(d, 0x130, 0);
    if (d->phase == 0xf) {
        mucb_set_header();
        MOBJManager_FadeOut(&d->apMobj[0], 0, 3, MUCB_MNG);
        __MUnitColorbar_Reset(pMobj);
        mucb_move(d, 0x130);
        d->phase = 0x10;
    }
    if (d->phase == 0x10) mucb_slide(d, 0x130, 0);
    if (d->phase == 0x17) {
        MoveMng_Init(&d->mng);
        d->phase = 0x18;
    }
    if (d->phase == 0x18) mucb_slide(d, -0x130, 0);
    if (d->phase == 0x19) {
        MoveMng_Init(&d->mng);
        d->phase = 0x1a;
    }
    if (d->phase == 0x1a) mucb_slide(d, 0x130, 0);
    if (d->phase == 0x11) {
        DF_MOBJ_SetDrawEnable(d->apMobj[2], 1);
        DF_MOBJ_SetFadeIn(d->apMobj[2]);
        d->phase = 0;
    }
    if (d->phase == 0x12) d->phase = 0;
}

/* tail jump; indexPast is ignored */
void MUnitColorbar_ChangeCursor(struct stMobj *pMobj, int indexNew, int indexPast)
{
    (void)indexPast;
    __MUnitColorbar_ChangeCursor(pMobj, indexNew);
}

int MUnitColorbar_IsMove(struct stMobj *pMobj)
{
    return CB(pMobj)->phase != 0;
}

static inline void mucb_set_draw_sprite(struct munit_colorbar_work *d)
{
    UnitSprite_GetChildObject(d->apMobj[0], 0)->draw_func = (decltype(d->apMobj[0]->draw_func))GFN(__MUnitColorbar_DrawSprite);
    UnitSprite_GetChildObject(d->apMobj[1], 0)->draw_func = (decltype(d->apMobj[1]->draw_func))GFN(__MUnitColorbar_DrawSprite);
}

void MUnitColorbar_FadeOutStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x30);
    struct munit_colorbar_work *d = CB(pMobj);

    d->phase = phase;
    if (d->phase == 0x17 || d->phase == 0x19) return;
    mucb_set_header();
    MOBJManager_FadeOut(&d->apMobj[0], 0, 3, MUCB_MNG);
    MoveMng_Init(&d->mng);
    mucb_set_draw_sprite(d);
}

void MUnitColorbar_FadeInStart(struct stMobj *pMobj, char phase)
{
    PS2_FRAME(0x30);
    struct munit_colorbar_work *d = CB(pMobj);

    d->phase = phase;
    if (d->phase == 0x17 || d->phase == 0x19) return;
    mucb_set_header();
    MOBJManager_FadeIn(&d->apMobj[0], 0, 3, MUCB_MNG);
    DF_MOBJ_SetDrawEnable(d->apMobj[2], 0);
    MoveMng_Init(&d->mng);
    mucb_set_draw_sprite(d);
}

void MUnitColorbar_Cleanup(struct stMobj *pMobj)
{
    PS2_FRAME(0x20);
    mucb_set_header();
    MOBJManager_Delete(&CB(pMobj)->apMobj[0], 3, MUCB_MNG);
}

struct stMobj *MUnitColorbar_NewSystem(struct stMobj *pParentMobj)
{
    PS2_FRAME(0x20);
    struct stMobj *p = DF_MOBJ_GetNewObject(pParentMobj);
    if (!p) return 0;

    memset(CB(p), 0, 0x24);
    p->type = 0;
    p->stat = 1;
    p->count = 0;
    p->play_func = (decltype(p->play_func))GFN(__MUnitColorbar_Play);
    p->draw_func = (decltype(p->draw_func))GFN(__MUnitColorbar_Draw);
    p->pkt_func = (decltype(p->pkt_func))GFN(__MUnitColorbar_Pkt);
    __MUnitColorbar_CreateItem(p);
    return p;
}
