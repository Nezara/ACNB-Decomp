/* src/enemy/ene004.cpp (OLSORTIE) - enemy type 4, a two-form boss: ground walker on the ene120 engine (Ene120_Initial
 * + e120_main + e094_control) that transforms into a flyer on the ene015 engine (e015_main / think / control,
 * interpreted) when message 6 arrives (Ene004_MsgFunc, vararg double != 0 -> flyer). State e_wk[0].c[0]: 0 = ground,
 * 1 = flyer, 2 = transforming to ground, 3 = transforming to flyer; e_wk[0].c[1] = last frame's state (the transform
 * controls start their animation when it differs). Ene004_Dying only forwards states 1 / 2 to e015_dying. */
#include "acnb/port.h"
#include "acnb/sdk.h"

#define E004_DEG 0x1.1df46ap-6f    /* 0x3c8efa35, 1 degree */

static void Ene004_Think(void)
{
}

static void __MsgTransformGround(EnemySt *pEne)
{
    PS2_FRAME(0x20);
    if (pEne->e_wk[0].c[0] != 0) {                   /* also restarts from states 2 / 3 */
        pEne->bst = 0;
        pEne->dir = 8;
        e015_ctrl_end(pEne, pEne->mp);
        pEne->thinkfunc = (void (*)(EnemySt *, StModel *))GFN(Ene004_Think);
        pEne->ctrlfunc = GFN(Ene004_Control);
        pEne->sw.flier = 0;
        pEne->e_wk[0].c[0] = 2;
    }
}

static void __MsgTransformFlyer(EnemySt *pEne)
{
    if (pEne->e_wk[0].c[0] != 1) {
        pEne->bst = 0;
        pEne->dir = 8;
        pEne->thinkfunc = (void (*)(EnemySt *, StModel *))GFN(Ene004_Think);
        pEne->ctrlfunc = GFN(Ene004_Control);
        pEne->sw.flier = 1;
        pEne->e_wk[0].c[0] = 3;
    }
}

static void __MsgTransform(EnemySt *pEne, int isFly)
{
    PS2_FRAME(0x10);
    if (isFly) __MsgTransformFlyer(pEne);
    else __MsgTransformGround(pEne);
}

/* real prototype (mp, msg, ...): the first vararg is a double (protofix arg0) */
static void Ene004_MsgFunc(void *ofp, int msg, unsigned long long arg0)
{
    PS2_FRAME(0x70);
    EnemySt *ep = (EnemySt *)((StModel *)ofp)->arg0p;
    if (ENE_ChkEnemyAddr(ep) < 0) return;
    if (msg == 6) __MsgTransform(ep, ps2_cmpdf2(arg0, 0) != 0);   /* 0x100440: __cmpdf2 != 0 (NaN -> 1) */
}

static void __TransFormGroundCtrl(EnemySt *pEne, StModel *pMdl)
{
    PS2_FRAME(0x40);
    ObjectSt *o1 = &pMdl->op[1];
    if (pEne->e_wk[0].c[0] != pEne->e_wk[0].c[1]) {
        Anim_SetAnim(pMdl, 0x1a, 0);
        DFene_ResetAnimLoop(pEne, pMdl);
    }
    /* decay toward zero by 1 degree - original bug kept: both tests compare against +D, so a < D gets +D */
    float a = o1->angle[0];
    if (!(a <= E004_DEG)) o1->angle[0] = a - E004_DEG;
    else if (a < E004_DEG) o1->angle[0] = a + E004_DEG;
    else o1->angle[0] = 0.0f;
    a = o1->angle[2];
    if (!(a <= E004_DEG)) o1->angle[2] = a - E004_DEG;
    else if (a < E004_DEG) o1->angle[2] = a + E004_DEG;
    else o1->angle[2] = 0.0f;
    if (DFene_IsAnimEnd(pEne, pMdl)) {
        pEne->thinkfunc = GFN(e120_think);
        pEne->ctrlfunc = GFN(e094_control);
        pEne->wep_valid &= 0xfb;
        pEne->wep_valid &= 0xef;
        pEne->wep_valid |= 8;
        Anim_SetAnim(pMdl, 0, 0);
        DFene_ResetAnimLoop(pEne, pMdl);
        pEne->e_wk[0].c[0] = 0;
    }
}

static void __TransFormFlyerCtrl(EnemySt *pEne, StModel *pMdl)
{
    PS2_FRAME(0x30);
    if (pEne->e_wk[0].c[0] != pEne->e_wk[0].c[1]) {
        Anim_SetAnim(pMdl, 0x18, 0);
        DFene_ResetAnimLoop(pEne, pMdl);
    }
    if (DFene_IsAnimEnd(pEne, pMdl)) {
        pEne->thinkfunc = GFN(e015_think);
        e015_ctrl_init(pEne, pMdl);                   /* sets ctrlfunc */
        pEne->wep_valid |= 4;
        pEne->wep_valid |= 0x10;
        pEne->wep_valid &= 0xf7;
        Anim_SetAnim(pMdl, 0x19, 0);
        DFene_ResetAnimLoop(pEne, pMdl);
        pEne->e_wk[0].c[0] = 1;
    }
}

static void Ene004_Control(EnemySt *pEne, StModel *pMdl)
{
    PS2_FRAME(0x10);
    switch ((unsigned char)pEne->e_wk[0].c[0]) {
    case 2: __TransFormGroundCtrl(pEne, pMdl); break;
    case 3: __TransFormFlyerCtrl(pEne, pMdl); break;
    default: break;
    }
}

static void Ene004_Main(EnemySt *pEne, StModel *pMdl)
{
    PS2_FRAME(0x30);
    unsigned char st = pEne->e_wk[0].c[0];
    if (st == 3 || st == 2) {
        ene_select_target(pEne, pMdl);
        if (pEne->thinkfunc && pEne->sw.think) gcall(pEne->thinkfunc, pEne, pMdl);
        enepad_PadManage(pEne);
        ene_get_lock(pEne, pMdl, pEne->cur_wep);
        if (pEne->ctrlfunc) gcall(pEne->ctrlfunc, pEne, pMdl);
        pEne->wep_valid &= 0xfe;                      /* no e120_main tail: new_pad is not cleared here */
    } else if (st == 1) {
        e015_main(pEne, pMdl);
    } else if (st == 0) {
        e120_main(pEne, pMdl);
    }
    pEne->e_wk[0].c[1] = pEne->e_wk[0].c[0];
}

void Ene004_Dying(EnemySt *pEne, StModel *pMdl)
{
    PS2_FRAME(0x10);
    unsigned char st = pEne->e_wk[0].c[0];
    if (st == 2 || st == 1) e015_dying(pEne, pMdl);  /* a dead ground-form (0) / state-3 boss runs nothing */
}

void Ene004_Init(EnemySt *pEne, StModel *pMdl)
{
    PS2_FRAME(0x40);
    EnemyDataSt *edat = pEne->edat;
    pMdl->msgfunc = (void (*)(void *, int))GFN(Ene004_MsgFunc);
    pEne->e_wk[0].c[0] = 0;
    pEne->e_wk[0].c[1] = 0;
    Ene120_Initial(pEne, pMdl);
    pEne->wep_valid &= 0xfb;
    pEne->wep_valid &= 0xef;
    pEne->wep_valid |= 8;
    ene_boost_sfx_init(pEne, pMdl, edat->sfx[0], edat->sfx[1]);   /* second call: Ene120_Initial did it too */
    DFene_SetFunc(pEne, (void *)GFN(Ene004_Main));
}
