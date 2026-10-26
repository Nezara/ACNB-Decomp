/* src/ac/acjcon.cpp - joint control: aims the AC's arm / weapon joints at a target from the joint-control file (per
 * control group a table of "turn object C, relative to parent P, toward the target within these limits at this
 * speed" records, plus a reaction kick when the AC is hit), and the per-AC joint controllers (a per-frame function with
 * a step / frame counter) used by the weapon code. */
#include "acnb/ps2fp.h"

/* object ids >= 0x10 skip 12 unused slots of the AC model */
static inline int jcon_objid(int id) { return id >= 0x10 ? id - 0xc : id; }

/* stop the reaction and the animation of every object in ctrlgroup (they blend to the joint control in hokantime) */
void acjcon_ResetReaction(AcPlayerSt *acp, int ctrlgroup, int hokantime)
{
    ObjectSt *op = acp->op;
    for (int i = 0; i < acp->mp->nobj; i++, op++) {
        if (!(op->group & ctrlgroup)) continue;
        op->noanim = 1;
        op->hokanframe = 0;
        op->hokantime = hokantime;
        op->todid = 0xff;
        struct_38955935 *jr = &acp->jointreact[i];
        jr->reaction_time = 0;
        jr->reaction_startframe = 0;
        jr->reaction_nframe = 0;
        jr->reactdamper = 1.0f;
        jr->angdif[0] = 0.0f;   /* angdif[3] is not cleared */
        jr->angdif[1] = 0.0f;
        jr->angdif[2] = 0.0f;
    }
}

/* one step of the controller; its frame counter runs while neither the function nor the step changed */
void acjcon_JointControl(AcPlayerSt *acp, _ac_jointctrl *jctrlp)
{
    PS2_FRAME(0x40);
    auto func = jctrlp->actfunc;
    if (!func) return;
    short step = jctrlp->actstep;
    gcall(func, (void *)acp, jctrlp, jctrlp->argp);
    if (jctrlp->actfunc == func && jctrlp->actstep == step) jctrlp->acttime++;
}

void acjcon_SetControl(_ac_jointctrl *jctrlp, void *func, _ac_weaponctrl *wctrlp)
{
    jctrlp->actstep = 0;
    jctrlp->acttime = 0;
    jctrlp->argp = wctrlp;
    jctrlp->actfunc = (decltype(jctrlp->actfunc))func;
}

void acjcon_Init(_ac_jointctrl *jctrlp)
{
    jctrlp->actstep = 0;
    jctrlp->acttime = 0;
    jctrlp->argp = 0;
    jctrlp->actfunc = 0;
}

/* One aiming step of every record of a control group. A joint in a hit reaction gets its kick angle (after the
 * reaction's start frame, at the reaction speed). Returns 1 when no joint is in a reaction. */
int acjcon_Play(AcPlayerSt *acp, float *target, JconHeadSt *jconheadp)
{
    PS2_FRAME(0x110);
    PS2_STACK(float[4], lim, 0xc0);
    PS2_STACK(float[4][4], lookmat, 0xd0);   /* row 3 is never written: stale stack carried into the joint angles */
    const float PI = 3.14159274f;   /* 0x40490fdb */
    int count = 0;
    struct_38f86b4e *rec = jconheadp->jcondattbl;
    int i;
    for (i = 0; i < jconheadp->nobj; i++, rec++) {
        ObjectSt *pobj = &acp->op[jcon_objid(rec->parent_objid)];
        int cid = jcon_objid(rec->control_objid);
        ObjectSt *obj = &acp->op[cid];
        struct_38955935 *jr = &acp->jointreact[cid];
        float xofs = 0.0f;
        float rotspd = rec->rotspeed * AcAsm_GetJconspdOwRate(&acp->param);
        if (jr->reaction_nframe != 0) {
            float on;
            if (jr->reaction_time - jr->reaction_startframe > 0) {
                on = 1.0f;
                rotspd = rec->reactspeed;
            } else on = 0.0f;
            int end = jr->reaction_startframe + jr->reaction_nframe;
            jr->reaction_time++;
            if (!(jr->reaction_time < end)) jr->reaction_nframe = 0;
            xofs = (jr->angdif[0] * on) * jr->reactdamper;
        } else count++;
        xofs = xofs + rec->lock_xang;
        if (rec->yvec_objid != -1) {
            float (*ym)[4][4] = acp->op[jcon_objid(rec->yvec_objid)].wrmatp;
            ObjLookWorldTarget2_Xofs(obj, *pobj->wrmatp, (*ym)[1], target, rec->rotlimits, rotspd, xofs);
        } else {
            for (int k = 0; k < 4; k++) lim[k] = (PI * rec->rotlimits[k]) / 180.0f;
            GetObjLookMatrix(lookmat, *pobj->wrmatp, obj->position, target, lim);
            DFvu0_MulRotMatrixX(lookmat, lookmat, (PI * xofs) / 180.0f);
            Matrix2AngleXZY_Approach(obj->angle, lookmat, rotspd);
        }
        Anim_UpdateMyObjMatrix(obj, pobj->wrmatp);
    }
    return !(count < jconheadp->nobj);
}

/* the joint-control file holds table offsets until it is relocated once */
void Map_JconData(struct_5a3002c3 *jfileheadp)
{
    JconFileHeadp = jfileheadp;
    if (jfileheadp->mapflag) return;
    for (unsigned int i = 0; i < jfileheadp->njcondata; i++) {
        JconHeadSt *h = &jfileheadp->jconheadtbl[i];
        h->jcondattbl = (struct_38f86b4e *)((uintptr_t)h->jcondattbl + (uintptr_t)jfileheadp);
    }
    jfileheadp->mapflag = 1;
}
