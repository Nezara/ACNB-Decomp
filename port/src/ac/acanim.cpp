/* src/ac/acanim.cpp - animation front end of an AC (whole file). Every AcAnim_ChangeAnim* variant first switches the
 * optional booster model (acp->boostmp) and sub-leg model (acp->sublegmp) to the animation matching the requested main
 * animation, then changes the main model's animation (model/anim.c). Also the dash / dash-brake / direction lookups on
 * the current animation of objects 14 and 1, group synchronisation, the model matrix refresh and the initial setup. */
#include "acnb/port.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
float IdentityMtx[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};   /* 0x277da0 */
#endif

/* booster-model animation for a main animation. original: the default is 0, never the -1 every caller tests for, so
 * an existing booster model always gets an animation change */
static int GetBoosterAnimId(int animid)
{
    if (!(animid < 0x11) && animid < 0x21) return animid - 0x10;
    if (animid == 0x24) return 0x11;
    if (!(animid < 0x27) && animid < 0x2f) return animid - 0x15;
    if (!(animid < 0x45) && animid < 0x47) return animid - 0x2b;
    return 0;
}

/* the booster / sub-leg preamble the original inlines verbatim in the five ChangeAnim* variants */
static void acanim_helper_models(AcPlayerSt *acp, int animid)
{
    int b = GetBoosterAnimId(animid);
    if (acp->boostmp && b != -1) Anim_ChangeAnimGroupXframe(acp->boostmp, 1, b, 0, 0x5a, 0);
    StModel *m = acp->sublegmp;
    int k;
    if (!m) {
        k = -1;
    } else {
        switch (acp->param.prog_no) {
        case 1:
            k = (animid == 0x41 || animid == 0x40 || animid == 0x3f || animid == 0x3e) ? 1 : 0;
            break;
        case 3:
            if (animid == 0x30) k = 2;
            else k = (animid == 0x4b || animid == 0x3a || animid == 0x38 || animid == 0x2f) ? 1 : 0;
            break;
        default:
            k = 0;
            break;
        }
    }
    if (m && k != -1) Anim_ChangeAnimGroupXframe(m, 1, k, 0, 0x5a, 0);
}

/* clears the animation of every object in groupbit, then restarts the group on trgobjid's animation / frame
 * (read after the clear: a target inside the group passes todid 0xff) */
void AcAnim_SynchroAnim(AcPlayerSt *acp, int trgobjid, int groupbit, int hokantime)
{
    ObjectSt *op = acp->op;
    for (int n = acp->mp->nobj; n > 0; n--, op++) {
        if (op->group & groupbit) {
            op->noanim = 0;
            op->todid = 0xff;
        }
    }
    if (trgobjid >= 0x10) trgobjid -= 0xc;
    ObjectSt *t = &acp->op[trgobjid];
    Anim_ChangeAnimGroupXframe(acp->mp, groupbit, t->todid, t->frame, hokantime, t->animloop);
}

int AcAnim_GetDashAnimId(AcPlayerSt *acp)
{
    switch (acp->op[14].todid) {
    case 1: case 2: return 0x11;
    case 3: case 4: return 0x13;
    case 5: case 6: return 0x14;
    case 7: case 8: return 0x15;
    case 9: case 10: return 0x16;
    case 11: case 12: return 0x12;
    case 13: case 14: return 0x17;
    case 15: case 16: return 0x18;
    }
    return 0;
}

int AcAnim_GetDashBrakeAnimId(AcPlayerSt *acp, int idDefault)
{
    unsigned int k = acp->op[14].todid - 0x11u;
    if (k < 0x10) return 0x19 + (int)(k & 7);
    return idDefault;
}

int AcAnim_GetAnim2Dir(AcPlayerSt *acp)
{
    unsigned int a = acp->op[1].todid;
    return a < 93 ? AcAnim_GetAnim2Dir__anim2dir[a] : 8;
}

int AcAnim_ChangeAnimGroupFrameStop(AcPlayerSt *acp, int groupbit, int animid, int startframe, int hokantime, int loop)
{
    return Anim_ChangeAnimGroupXframeStop(acp->mp, groupbit, animid, startframe, hokantime, loop);
}

int AcAnim_ChangeAnimGroup(AcPlayerSt *acp, int groupbit, int animid, int hokantime, int loop)
{
    return Anim_ChangeAnimGroupXframe(acp->mp, groupbit, animid, 0, hokantime, loop);
}

/* start frame -1: keep the current frame */
int AcAnim_ChangeAnimFrameSame(AcPlayerSt *acp, int animid, int hokantime, int loop)
{
    acanim_helper_models(acp, animid);
    return Anim_ChangeAnimGroupXframe(acp->mp, 1, animid, -1, hokantime, loop);
}

int AcAnim_ChangeAnimFrameLast(AcPlayerSt *acp, int animid, int hokantime)
{
    acanim_helper_models(acp, animid);
    return Anim_ChangeAnimLastFrame(acp->mp, animid, hokantime);
}

int AcAnim_ChangeAnimFrameStop(AcPlayerSt *acp, int animid, int startframe, int hokantime, int loop)
{
    acanim_helper_models(acp, animid);
    return Anim_ChangeAnimGroupXframeStop(acp->mp, 1, animid, startframe, hokantime, loop);
}

int AcAnim_ChangeAnimFrame(AcPlayerSt *acp, int animid, int startframe, int hokantime, int loop)
{
    acanim_helper_models(acp, animid);
    return Anim_ChangeAnimGroupXframe(acp->mp, 1, animid, startframe, hokantime, loop);
}

int AcAnim_ChangeAnim(AcPlayerSt *acp, int animid, int hokantime, int loop)
{
    acanim_helper_models(acp, animid);
    return Anim_ChangeAnimGroupXframe(acp->mp, 1, animid, 0, hokantime, loop);
}

/* model matrix from ang / loc, copied to the root object; the child objects follow (inlined twice in the original) */
static void acanim_update_matrix(StModel *mp)
{
    sceVu0RotMatrix(mp->matrix2, IdentityMtx, mp->ang);
    sceVu0TransMatrix(mp->matrix2, mp->matrix2, mp->loc);
    sceVu0CopyMatrix(*mp->op[0].wrmatp, mp->matrix2);
    if (mp->nobj >= 2) Anim_UpdateChildObjMatrix(&mp->op[1], mp->matrix2);
}

void AcAnim_UpdateAllObjMatrix(StModel *mp)
{
    acanim_update_matrix(mp);
}

int AcAnim_InitModelAnimation(StModel *mp)
{
    if (!mp) return 0;
    for (int i = 0; i < mp->nobj; i++) {
        mp->op[i].todid = 0xff;
        mp->op[i].nframe = 1;
        mp->op[i].group = 1;
    }
    Anim_SetAnim(mp, 0, 0);
    Anim_Update(mp);
    acanim_update_matrix(mp);
    return 1;
}

#ifdef ACNB_SOURCEPORT
int AcAnim_GetAnim2Dir__anim2dir[93] = {
    8, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,   /* 0-16 */
    0, 5, 1, 2, 3, 4, 6, 7,                              /* 17-24 */
    0, 5, 1, 2, 3, 4, 6, 7,                              /* 25-32 */
    8, 8, 8, 8, 8, 8,                                    /* 33-38 */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 8,                        /* 39-48 */
    0, 0, 8, 5, 0, 4, 3,                                 /* 49-55 */
    8, 8, 8, 8, 8, 8, 8,                                 /* 56-62 */
    0, 1, 2, 0, 2, 1, 8, 8, 5, 0, 4, 3, 8, 8, 8,         /* 63-77 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,         /* 78-92 */
};
#endif
