/* src/stat/fade.cpp - per-view screen fades (whole file). procid (s_pFadeProcTab): 0 normal, 1 pause dim, 2 fade in,
 * 3 fade out, 4 black-out. Each fade runs a ctrl step (Exec) and a draw step (Draw: one full-screen sprite);
 * DF_FADE_Stop jumps to a procid's end state.
 * Quirks: DF_FADE_Init zeroes only the first 0x34 bytes of the manager (num and part of fade[0]);
 * DF_FADE_SetProc accepts procid 5 although s_pFadeProcTab has 5 entries (reads the 0 word after the table: no
 * ctrl); the id checks are signed (id < 4). */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

void DF_FADE_Stop(int id)
{
    if (id >= 4) return;
    __fade_st *f = &s_FadeMng.fade[id];
    switch (f->procid) {
    case 3:   /* fade out */
        f->blend = 2;
        f->dstate = 1;
        f->rgbai[0] = 0xff;
        f->rgbai[1] = 0xff;
        f->rgbai[2] = 0xff;
        f->rgbai[3] = 0x80;
        break;
    case 2:   /* fade in */
        f->blend = 2;
        f->dstate = 1;
        f->rgbai[0] = 0;
        f->rgbai[1] = 0;
        f->rgbai[2] = 0;
        f->rgbai[3] = 0x80;
        break;
    case 1:   /* pause dim */
        f->blend = 0;
        f->dstate = 1;
        f->rgbai[0] = 0;
        f->rgbai[1] = 0;
        f->rgbai[2] = 0;
        f->rgbai[3] = 0x3f;
        break;
    case 0:
        f->dstate = 0;
        break;
    }
    f->state = 4;
}

/* state: 1 start, 2 running, 3 finished (-> 4 next step), 4 stopped */
static void __FADE_CtrlBlackOut(__fade_st *fade)
{
    switch (fade->state) {
    case 4:
        return;
    case 1:
        fade->rgbai[0] = 0;
        fade->rgbai[1] = 0;
        fade->rgbai[2] = 0;
        fade->rgbai[3] = 0x80;
        fade->blend = 2;
        fade->dstate = 1;
        fade->state = 2;
        fade->now_frame = 0;
        /* fall through */
    case 2:
        fade->now_frame++;
        if (fade->now_frame >= fade->add) {
            fade->rgbai[3] = 0;
            fade->state = 3;
        }
        return;
    default:
        fade->state = 4;
        return;
    }
}

static void __FADE_CtrlFadeOut(__fade_st *fade)
{
    switch (fade->state) {
    case 4:
        return;
    case 1:
        fade->rgbai[0] = 0;
        fade->rgbai[1] = 0;
        fade->rgbai[2] = 0;
        fade->rgbai[3] = 0;
        fade->blend = 0;
        fade->dstate = 1;
        fade->state = 2;
        fade->now_frame = 0;
        /* fall through */
    case 2: {
        fade->now_frame++;
        float t = fade->add ? (float)fade->now_frame / (float)fade->add : 0.0f;
        fade->rgbai[3] = ps2_ftoi(128.0f * t);
        if (fade->now_frame >= fade->add) {
            fade->rgbai[3] = 0x80;
            fade->state = 3;
        }
        return;
    }
    default:
        fade->state = 4;
        return;
    }
}

static void __FADE_CtrlFadeIn(__fade_st *fade)
{
    switch (fade->state) {
    case 4:
        return;
    case 1:
        fade->rgbai[0] = 0;
        fade->rgbai[1] = 0;
        fade->rgbai[2] = 0;
        fade->rgbai[3] = 0x80;
        fade->blend = 0;
        fade->dstate = 1;
        fade->state = 2;
        fade->now_frame = 0;
        /* fall through */
    case 2: {
        fade->now_frame++;
        float t = fade->add ? 1.0f - (float)fade->now_frame / (float)fade->add : 1.0f;
        fade->rgbai[3] = ps2_ftoi(128.0f * t);
        if (fade->now_frame >= fade->add) {
            fade->rgbai[3] = 0;
            fade->state = 3;
        }
        return;
    }
    default:
        fade->state = 4;
        return;
    }
}

static void __FADE_CtrlPause(__fade_st *fade)
{
    if (fade->state != 1) return;
    fade->rgbai[0] = 0;
    fade->rgbai[1] = 0;
    fade->rgbai[2] = 0;
    fade->rgbai[3] = 0x3f;
    fade->blend = 0;
    fade->dstate = 1;
    fade->state = 4;
}

static void __FADE_CtrlNormal(__fade_st *fade)
{
    fade->state = 4;
    fade->dstate = 0;
}

static void __FADE_DrawNormal(__fade_st *fade)
{
    PS2_FRAME(0x20);
    if (!fade->dstate) return;
    ps2_rq_tag_scope(RQ_K_HUD, RQ_OBJID(RQ_OC_HUD_ELEM, 7), 7); GS_NewList();
    GS_SetALPHA_1(0, 1, 0, 1, 0);
    GS_SetTEST_1(0, 0, 0, 0, 0, 0, 1, 1);
    GS_SetRGBA(fade->rgbai);
    GS_DrawSpriteS(0, 0, 0x200, 0x1c0);
    GS_FlushList(); ps2_rq_tag_end();
}

int DF_FADE_IsStop(int id)
{
    if (id >= 4) return 0;
    char st = s_FadeMng.fade[id].state;
    return st == 0 || st == 4 || st == 3;
}

/* add = duration in frames; refused (-1) while a fade is starting / running / just finished */
int DF_FADE_SetProc(int id, int procid, unsigned short add)
{
    if (id >= 4) return -1;
    __fade_st *f = &s_FadeMng.fade[id];
    if (f->state == 3 || f->state == 1 || f->state == 2) return -1;
    if ((unsigned int)procid >= 6) return -1;
    f->procid = procid;
    f->ctrl = (decltype(f->ctrl))s_pFadeProcTab[procid];
    f->add = add;
    f->now_frame = 0;
    f->dstate = 0;
    f->state = 1;
    return 0;
}

void DF_FADE_Draw(void)
{
    PS2_FRAME(0x30);
    __fade_st *f = s_FadeMng.fade;
    for (int i = 0; i < s_FadeMng.num; i++, f++) {
        FlushCache(0);
        if (f->draw) gcall(f->draw, f);
    }
}

void DF_FADE_Exec(void)
{
    PS2_FRAME(0x30);
    __fade_st *f = s_FadeMng.fade;
    for (int i = 0; i < s_FadeMng.num; i++, f++)
        if (f->ctrl) gcall(f->ctrl, f);
}

void DF_FADE_Init(int num)
{
    PS2_FRAME(0x20);
    ZeroMemory32b(&s_FadeMng, 0x34);
    s_FadeMng.num = (char)num;
    for (int i = 0; i < 4; i++) {
        __fade_st *f = &s_FadeMng.fade[i];
        f->state = 4;
        f->draw = (decltype(f->draw))GFN(__FADE_DrawNormal);
        f->ctrl = (decltype(f->ctrl))GFN(__FADE_CtrlNormal);
    }
}

#ifdef ACNB_SOURCEPORT
/* the original's initialized tables (generated by scripts/sp_datagen.py from the PS2 image) */
void (*s_pFadeProcTab[5])(struct __fade_st *) = {   /* main 0x2629f0 */
    __FADE_CtrlNormal, __FADE_CtrlPause, __FADE_CtrlFadeIn, __FADE_CtrlFadeOut, __FADE_CtrlBlackOut
};
#endif
