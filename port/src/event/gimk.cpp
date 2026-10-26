/* src/event/gimk.cpp - stage "gimmicks": map / object / dummy models whose array eid names a gimmick type get a
 * 0x40-byte GimkSt (s_pGimk[s_GimkNum], carved out of the stage buffer by DF_GIMK_Alloc, followed by the 0x1000-byte
 * buffer of 256 exec-list nodes). InitAllGimk hooks each model (mp->arg0p = its GimkSt, mp->msgfunc =
 * __gimk_ReceiveMsg) and runs the type's init from s_GimkInitTab[eid]: the inits copy the model's typedata into the
 * gimk's 0x30-byte work area (handed out front to back, wk_empty = bytes left) and append per-frame callbacks to a
 * singly linked exec list (node 0 = head sentinel; fflag bit 0 = run in the second, "late" pass). ExecGimk walks the
 * list twice per frame. Hits, bombs, "an AC stands on me", the event button, pause and kill reach a gimk as messages
 * (__gimk_ReceiveMsg). Types (= eid): 2 roll, 4 texture scroll, 6 sfx, 8 move, 0xa roll + texture scroll, 0xc model
 * attached to another model's object, 0x10-0x16 destructible, 0x18 destructible with debris physics (DestPG), 0x1a
 * its sfx data, 0x20 animation, 0x30 building, 0x32 elevator, 0x34 elevator switch, 0x36 supply truck, 0x38 hidden
 * parts. In Versus only texture-scroll gimks run; everything else is mission / event-stage code. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

#ifdef ACNB_SOURCEPORT
void (*s_GimkInitTab[128])(struct GimkSt *) = {
    nullptr, nullptr, __gimk_InitRoll, nullptr, __gimk_InitTexScroll, nullptr, __gimk_InitSfx, nullptr,
    __gimk_InitMoveModel, nullptr, __gimk_InitRollModel_TexScroll, nullptr, __gimk_InitModelAttach, nullptr, nullptr,
    nullptr,
    __gimk_InitDestruct, nullptr, __gimk_InitDestruct, nullptr, __gimk_InitDestruct, nullptr, __gimk_InitDestruct,
    nullptr, __gimk_InitDestPG, nullptr, (void (*)(GimkSt *))__gimk_InitDestSfx, nullptr, nullptr, nullptr, nullptr,
    nullptr,
    __gimk_InitAnim, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    __gimk_InitBuilt, nullptr, __gimk_InitElevator, nullptr, __gimk_InitElevatorSwitch, nullptr, __gimk_InitCar,
    nullptr, __gimk_InitHiddenParts,
};
/* destruction thresholds (hit damage / heat by the data's destDmgID / destHeatID) and debris coefficients */
unsigned short s_DestDmg[6] = {0, 100, 250, 1000, 2000, 4000};
unsigned short s_DestHeat[6] = {0, 2, 10, 100, 500, 1000};
float s_DestPGMassCof[5] = {0.0164f, 0.0327f, 0.0653f, 0.098f, 0.163f};
float s_DestPGElstCof[5] = {0.9f, 0.5f, 0.3f, 0.15f, 0.0f};
float s_DestPGPowerCof[5] = {0.0f, 0.5f, 1.0f, 2.0f, 5.0f};
int s_DestPGTransCof[5] = {30, 10, 5, 1, 0};
#endif

/* newlib abs (no overflow trap) */
static inline int gimk_abs(int v) { return v < 0 ? (int)(0u - (unsigned int)v) : v; }

/* random value in [0, n): rand() % (n << 14) >> 14 (signed remainder) */
static inline int gimk_rand_range(int n) { return ps2_mod32(ps2_rand(), n << 14) >> 14; }

/* start frame of a typedata frame field: v >= 0 as is, v < 0 = random in [0, |v|) */
static int gimk_start_frame(int v)
{
    if (v >= 0) return v;
    if (gimk_abs(v) > 0) return gimk_rand_range(gimk_abs(v));
    return 0;
}

/* n bytes of the gimk's work area (front to back); over-allocation only reports */
static char *gimk_wk_alloc(GimkSt *pGimk, int n)
{
    if (pGimk->wk_empty < n) DF_GIMK_ErrorMSG(/* "GimkWorkBuffOver" */);
    unsigned int old = pGimk->wk_empty;
    pGimk->wk_empty = old - n;
    return (char *)pGimk + (0x30 - old) + 0x10;
}

/* appends a callback to the exec list (one node is never handed out); NULL when the list is full */
static GimkFuncListSt *gimk_add_exec(GimkSt *pGimk, void (*fn)(GimkSt *), int fflag)
{
    GimkFuncListSt *n = s_pGimkEmptyListFirst;
    if (n == s_pGimkEmptyListLast) {
        DF_GIMK_ErrorMSG(/* "Over Flow Gimk Exec Buff" */);
        return nullptr;
    }
    s_pGimkEmptyListFirst = n->pNext;
    s_pGimkExecListLast->pNext = n;
    s_pGimkExecListLast = n;
    n->fflag = fflag;
    n->pGimk = pGimk;
    n->pExec = fn;
    n->pNext = nullptr;
    return n;
}

/* builds the 256-node list: node 0 = exec-list head, 1..255 = free list */
static void __gimk_InitFunc(void)
{
    PS2_FRAME(0x10);
    GimkFuncListSt *node = s_pGimkExecListFirst;
    if (!node) {
        DF_GIMK_ErrorMSG(/* "No GimkFunc Buffer" */);
        return;
    }
    for (int i = 0; i < 256; i++) {
        node[i].index = i;
        node[i].fflag = 0;
        node[i].pGimk = nullptr;
        node[i].pExec = nullptr;
        node[i].pNext = i < 255 ? &node[i + 1] : nullptr;
    }
    GimkFuncListSt *first = s_pGimkExecListFirst;
    s_pGimkEmptyListLast = &node[255];
    s_pGimkExecListLast = first;
    s_pGimkEmptyListFirst = first + 1;
    first->pNext = nullptr;
    s_pGimkExecListLast->pNext = nullptr;
}

/* array eids that are not gimmicks (event buttons, enemies, ...) */
static inline int gimk_is_gimmick(unsigned int e)
{
    return e && e != 0x60 && e != 0x61 && e != 0x62 && e != 0x50 && e != 0x70 && e != 0x71 && e != 0x72 && e != 0x73;
}

static char *__gimk_Alloc(char *ptr, ArraySt *arr, StModel *mp, int num)
{
    for (int i = 0; i < num; i++, arr++, mp++) {
        if (!gimk_is_gimmick(arr->eid)) continue;
        ((GimkSt *)ptr)->mp = mp;
        ptr += 0x40;
    }
    return ptr;
}

int GetGimkCount(ArraySt *arr, int num)
{
    int n = 0;
    for (int i = 0; i < num; i++, arr++)
        if (gimk_is_gimmick(arr->eid)) n++;
    return n;
}

/* the GimkSt array for the map, object and dummy models, then the exec-list buffer; returns the end of both */
char *DF_GIMK_Alloc(char *buff, ArraySt *marr, StModel *mmp, int mnum, ArraySt *oarr, StModel *omp, int onum,
                    ArraySt *darr, StModel *dmp, int dnum)
{
    PS2_FRAME(0x40);
    if (!buff) {
        s_pGimk = nullptr;
        s_GimkNum = 0;
        return nullptr;
    }
    char *p = __gimk_Alloc(buff, marr, mmp, mnum);
    p = __gimk_Alloc(p, oarr, omp, onum);
    p = __gimk_Alloc(p, darr, dmp, dnum);
    if ((uintptr_t)buff < (uintptr_t)p) {
        s_pGimk = (GimkSt *)buff;
        s_GimkNum = (unsigned int)(p - buff) >> 6;
    } else {
        s_pGimk = nullptr;
        s_GimkNum = 0;
    }
    if (!p) return nullptr;
    if ((uintptr_t)p & 0xf) {
        DF_GIMK_ErrorMSG(/* "GimkFuncBuffer Aligned Error" */);
        return nullptr;
    }
    s_pGimkExecListFirst = (GimkFuncListSt *)p;
    return p + 0x1000;
}

#ifndef ACNB_SOURCEPORT
/* oracle: MIPS -> native wrapper (gen_bridge stops at 8 int args); dmp / dnum are the caller's stack args at sp+0 /
 * sp+8 */
static void bw__DF_GIMK_Alloc(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    char *r = DF_GIMK_Alloc((char *)bridge_ptr(c->gpr[A0].uw[0]), (ArraySt *)bridge_ptr(c->gpr[A1].uw[0]),
                            (StModel *)bridge_ptr(c->gpr[A2].uw[0]), c->gpr[A3].sw[0],
                            (ArraySt *)bridge_ptr(c->gpr[T0].uw[0]), (StModel *)bridge_ptr(c->gpr[T1].uw[0]),
                            c->gpr[T2].sw[0], (ArraySt *)bridge_ptr(c->gpr[T3].uw[0]),
                            (StModel *)bridge_ptr(rd32(sp)), (int)rd32(sp + 8));
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

void InitAllGimk(void)
{
    PS2_FRAME(0x50);
    __gimk_InitFunc();
    GimkSt *g = s_pGimk;
    for (int n = s_GimkNum; n--; g++) {
        StModel *mp = g->mp;
        unsigned int t = mp->array->eid;
        ZeroMemory32b(g, 0x10);
        g->mp = mp;
        g->type = t;
        g->sflag = 0;
        g->wk_empty = 0x30;
        mp->arg0p = g;
        mp->msgfunc = (void (*)(void *, int))GFN(__gimk_ReceiveMsg);
        if (s_GimkInitTab[t]) gcall(s_GimkInitTab[t], g);   /* no bound check on eid */
    }
}

/* returns a node to the free list; pNext is cleared, so a pass-1 walk stops after the first removal */
static void gimk_unlink(GimkFuncListSt *prev, GimkFuncListSt *n)
{
    if (n == s_pGimkExecListFirst) return;
    if (s_pGimkExecListLast == n) s_pGimkExecListLast = prev;
    prev->pNext = n->pNext;
    s_pGimkEmptyListLast->pNext = n;
    s_pGimkEmptyListLast = n;
    n->fflag = 0;
    n->pGimk = nullptr;
    n->pExec = nullptr;
    n->pNext = nullptr;
}

/* per frame: pass 1 runs the normal callbacks and drops dead gimks / removal requests, pass 2 the late ones */
void ExecGimk(void)
{
    PS2_FRAME(0x20);
    GimkFuncListSt *prev = s_pGimkExecListFirst;
    GimkFuncListSt *n = prev->pNext;
    if (!n || !n->pGimk) return;
    do {
        GimkSt *g = n->pGimk;
        unsigned int s = g->sflag;
        if (s == 2) gimk_unlink(prev, n);
        else if (s == 0) {
            if (n->fflag & 0x80) gimk_unlink(prev, n);
            else if (!(n->fflag & 1)) gcall(n->pExec, g);
        }
        prev = n;
        n = n->pNext;
    } while (n);

    n = s_pGimkExecListFirst->pNext;
    if (!n || !n->pGimk) return;
    do {
        GimkSt *g = n->pGimk;
        if (!g->sflag && (n->fflag & 1)) gcall(n->pExec, g);
        n = n->pNext;
    } while (n);
}

/* event button pressed on a gimk (player AC not in action 0x12 / actstat bit 0): hidden parts, supply truck,
 * elevator switch */
static int gimk_event(GimkSt *pGimk);

int ExecGimkEvent(GimkSt *pGimk)
{
    PS2_FRAME(0x20);
    if (!pGimk) return -1;
    return gimk_event(pGimk);
}

static void __gimk_InitTexScroll(GimkSt *pGimk)
{
    PS2_FRAME(0x40);
    __gimk_tex_st_st *wk = (__gimk_tex_st_st *)gimk_wk_alloc(pGimk, 0xc);
    __data_tex_st_st *td = (__data_tex_st_st *)pGimk->mp->array->typedata;
    wk->tex_s = td->tex_s;
    wk->tex_t = td->tex_t;
    wk->frame = gimk_start_frame(td->frame);
    gimk_add_exec(pGimk, GFN(__gimk_ExecTexScroll), 0);
}

/* one texture-scroll axis: tex > 0 counts up, tex < 0 down, period |tex| frames */
static void gimk_scroll(float *st, float f, int tex)
{
    if (tex > 0) *st = f * (float)(FRAME % (unsigned int)tex);
    if (tex < 0) {
        int a = gimk_abs(tex);
        int b = gimk_abs(tex);
        *st = f * (float)(unsigned int)(a - FRAME % (unsigned int)b);
    }
}

static void __gimk_ExecTexScroll(GimkSt *pGimk)
{
    __gimk_tex_st_st *wk = (__gimk_tex_st_st *)pGimk->wk;
    float fs = ps2_div(1.0f, (float)gimk_abs(wk->tex_s));
    float ft = ps2_div(1.0f, (float)gimk_abs(wk->tex_t));
    if (FRAME < (unsigned int)wk->frame) return;
    gimk_scroll(&pGimk->mp->vuparam.addST[0], fs, wk->tex_s);
    gimk_scroll(&pGimk->mp->vuparam.addST[1], ft, wk->tex_t);
}

/* ---- shared pieces of the per-type code ---- */

#define GIMK_TD(g) ((g)->mp->array->typedata)

/* DEG2RAD / RAD2DEG / PI / 2PI as the original's constants */
#define GIMK_DEG2RAD ps2_u2f(0x3c8efa35)
#define GIMK_RAD2DEG ps2_u2f(0x42652ee1)
#define GIMK_PI ps2_u2f(0x40490fdb)
#define GIMK_2PI ps2_u2f(0x40c90fdb)

/* stops a type-6 gimk's effect (magic or spell handle) */
static void gimk_sfx_kill(__gimk_sfx_st *s)
{
    if (!s->m_or_s) DF_MAGIC2_Kill_ID(s->hSfx);
    else DF_SPELL2_KillSpell((struct _spell2 *)(uintptr_t)(unsigned int)s->hSfx);
    s->run = 0;
    s->hSfx = 0;
}

/* tail of pause / kill: a running sfx gimk loses its effect (elevators and cars keep their state) */
static void gimk_kill_tail(GimkSt *pGimk)
{
    if (pGimk->type == 0x36 || pGimk->type == 0x32) return;
    if (pGimk->type != 6) return;
    __gimk_sfx_st *s = (__gimk_sfx_st *)pGimk->wk;
    if (s->run) gimk_sfx_kill(s);
}

static void gimk_kill(GimkSt *pGimk)
{
    pGimk->sflag = 2;
    gimk_kill_tail(pGimk);
}

/* one animation step; a model following the player's layer (mflag bit 0) takes the player's group and is drawn on
 * layer 3. 0 = not on a layer, nothing done. */
static int gimk_anim_step(StModel *mp)
{
    if (!mp->on_layer && !(mp->mflag & 1)) return 0;
    if (mp->mflag & 1) {
        mp->groupid = g_ac[0].mp->nowparent->groupid;
        mp->on_layer = 3;
        Anim_AnimMi2TodUpdateN(mp);
    } else
        Anim_AnimMi2TodUpdate(mp);
    return 1;
}

/* MWCC's angle wrap loops (memory resident; a NaN would loop forever) */
static void gimk_angle_wrap(float *a)
{
    while (!(*a <= GIMK_PI)) *a = *a - GIMK_2PI;
    while (*a < -GIMK_PI) *a = *a + GIMK_2PI;
}

/* model matrix = mat, child objects, location = mat row 3, bounding sphere centre and height range moved along */
static void gimk_set_matrix(StModel *d, float (*mat)[4], float *ctr)
{
    CopyMemory32b(d->matrix2, mat, 0x10);
    Anim_UpdateChildObjMatrix(d->op, d->matrix2);
    memcpy(d->loc, mat[3], 16);
    float oldy = d->mlist->sphere[1];
    vu_apply(ctr, (const float (*)[4])d->matrix2, d->localcenter);
    d->mlist->sphere[0] = ctr[0];
    d->mlist->sphere[1] = ctr[1];
    d->mlist->sphere[2] = ctr[2];
    float dy = d->mlist->sphere[1] - oldy;
    d->Ytop = d->Ytop + dy;
    d->Ybottom = d->Ybottom + dy;
}

/* ---- messages ---- */

/* event button (msg 0 / ExecGimkEvent without the NULL test) */
static int gimk_event(GimkSt *pGimk)
{
    if (g_ac[0].actno == 0x12) return -1;
    if (g_ac[0].actstat & 1) return -1;
    switch (pGimk->type) {
    case 0x38: {
        StModel *mp = pGimk->mp;
        __data_hide_parts_st *td = (__data_hide_parts_st *)mp->array->typedata;
        mp->use |= 3;
        MsgWnd_ShowCom(0x11, nullptr);
        Voice_CallPoint(0xc00, 0x3c007f, (float (*)[4])g_ac[0].loc, 250.0f);
        Game_Sitmp_PushSecretParts(0, td->kind, td->id, td->sfg_id, td->clear, 0);
        return 0;
    }
    case 0x36: return __gimk_ExecCarEvent(pGimk);
    case 0x34: return __gimk_ExecElevatorSwitch(pGimk);
    }
    return -1;
}

/* mp->msgfunc of every gimmick model (variadic in the original; msg 1 / 2 read the first two varargs):
 * 0 event button, 1 an AC stands on it, 2 hit, 3 resume, 4 pause, 5 kill. Hit handlers get (hloc = 2nd vararg,
 * dstat = 1st); the spell senders pass (pos, dstat), so their hits arrive swapped (original quirk). */
static void __gimk_ReceiveMsg(void *pGimk, int msg, void *arg0, void *arg1)
{
    PS2_FRAME(0x70);
    GimkSt *g = (GimkSt *)pGimk;
    if (!g || g->sflag == 2 || (unsigned int)msg >= 6) return;
    switch (msg) {
    case 0:
        gimk_event(g);
        break;
    case 1:
        if (!(g->gflag & 2)) return;
        switch (g->type) {
        case 0x32: {
            struct_5b2f7e9e *e = (struct_5b2f7e9e *)g->wk;
            __data_elv_st *td = (__data_elv_st *)GIMK_TD(g);
            e->ride = 0;
            if (!td->sw && FRAME < (unsigned int)(e->frame + td->wait_frame)) return;
            e->ride_frame++;
            break;
        }
        case 0x36:
            ((__gimk_car_st *)g->wk)->ride_on = 1;
            break;
        case 0x16: case 0x14: case 0x12: case 0x10:
            DF_GIMK_ModelBomb(g);
            break;
        }
        break;
    case 2: {
        float *hloc = (float *)arg1;
        SfxDstat *dstat = (SfxDstat *)arg0;
        switch (g->type) {
        case 0x30: __gimk_ExecBuilt(g, hloc); break;
        case 0x18: __gimk_ExecDestPG(g, hloc, dstat); break;
        case 0x36: case 0x16: case 0x14: case 0x12: case 0x10: __gimk_ExecDestruct(g, hloc, dstat); break;
        case 0xc: __gimk_ExecModelAttachHit(g, hloc, dstat); break;
        }
        break;
    }
    case 3:
        g->sflag = 0;
        break;
    case 4:
        g->sflag = 1;
        gimk_kill_tail(g);
        break;
    case 5:
        gimk_kill(g);
        break;
    }
}

/* ---- hidden parts, animation, move, roll ---- */

static void __gimk_InitHiddenParts(GimkSt *pGimk)
{
    PS2_FRAME(0x20);
    if (Game_IsAchievement(0, ((__data_hide_parts_st *)GIMK_TD(pGimk))->sfg_id)) pGimk->mp->use |= 3;
}

static void __gimk_InitAnim(GimkSt *pGimk)
{
    PS2_FRAME(0x50);
    __gimk_anim_st *wk = (__gimk_anim_st *)gimk_wk_alloc(pGimk, 8);
    __data_anim_st *td = (__data_anim_st *)GIMK_TD(pGimk);
    wk->loop = td->loop;
    Anim_SetAnimId(pGimk->mp, td->anim_id);
    Anim_SetLoop(pGimk->mp, td->loop ? 1 : 0);
    StXtod *t = pGimk->mp->tod;
    if (!((unsigned int)gimk_abs(td->frame) < t->head[td->anim_id]->frame))
        wk->frame = gimk_start_frame(td->frame);
    else {
        /* start inside the animation instead of waiting */
        pGimk->mp->tod->nowframe = gimk_start_frame(td->frame);
        wk->frame = 0;
    }
    gimk_add_exec(pGimk, GFN(__gimk_ExecAnim), 0);
}

static void __gimk_ExecAnim(GimkSt *pGimk)
{
    PS2_FRAME(0x30);
    __gimk_anim_st *wk = (__gimk_anim_st *)pGimk->wk;
    if (FRAME < (unsigned int)wk->frame) return;
    if (!gimk_anim_step(pGimk->mp)) return;
    if (wk->loop) return;
    if (Anim_IsEndTod(pGimk->mp) != 1) return;
    gimk_kill(pGimk);
}

static void __gimk_InitMoveModel(GimkSt *pGimk)
{
    PS2_FRAME(0x20);
    __gimk_move_st *wk = (__gimk_move_st *)gimk_wk_alloc(pGimk, 0x30);
    __data_move_st *td = (__data_move_st *)GIMK_TD(pGimk);
    wk->speed[0] = td->speed_x;
    wk->speed[1] = td->speed_y;
    wk->speed[2] = td->speed_z;
    wk->speed[3] = 1.0f;
    wk->pos[0] = td->pos_x;
    wk->pos[1] = td->pos_y;
    wk->pos[2] = td->pos_z;
    wk->pos[3] = 1.0f;
    wk->end_pos = td->end_pos;
    wk->type = td->type;
    gimk_add_exec(pGimk, GFN(__gimk_ExecMoveModel), 0);
}

/* moves along speed; back to the start position once the axis (type 1 x, 2 y, 3 z) passes end_pos */
static void __gimk_ExecMoveModel(GimkSt *pGimk)
{
    PS2_FRAME(0x30);
    __gimk_move_st *wk = (__gimk_move_st *)pGimk->wk;
    DF_CALC_MoveModel(pGimk->mp, wk->speed);
    int k = wk->type - 1;
    if ((unsigned int)k >= 3) return;
    StModel *mp = pGimk->mp;
    int reset;
    if (!(wk->speed[k] <= 0.0f)) reset = !(mp->loc[k] <= wk->end_pos);
    else reset = mp->loc[k] < wk->end_pos;
    if (reset) DF_MDL_SetModel(mp, wk->pos, mp->array->ang, mp->array->scale);
}

static void __gimk_InitRoll(GimkSt *pGimk)
{
    PS2_FRAME(0x40);
    __gimk_roll_st *wk = (__gimk_roll_st *)gimk_wk_alloc(pGimk, 0x30);
    __data_roll_st *td = (__data_roll_st *)GIMK_TD(pGimk);
    const float d2r = GIMK_DEG2RAD;
    wk->rot[0] = d2r * td->rot_x;
    wk->rot[1] = d2r * td->rot_y;
    wk->rot[2] = d2r * td->rot_z;
    wk->rot[3] = 1.0f;
    wk->min_x = (float)td->min_x;
    wk->max_x = (float)td->max_x;
    wk->min_y = (float)td->min_y;
    wk->max_y = (float)td->max_y;
    wk->min_z = (float)td->min_z;
    wk->max_z = (float)td->max_z;
    wk->frame = gimk_start_frame(td->frame);
    gimk_add_exec(pGimk, GFN(__gimk_ExecRoll), 0);
}

/* rotates by rot; an axis with a [min, max] range (degrees off the array angle) turns back at its ends */
static void __gimk_ExecRoll(GimkSt *pGimk)
{
    PS2_FRAME(0x10);
    __gimk_roll_st *wk = (__gimk_roll_st *)pGimk->wk;
    if (FRAME < (unsigned int)wk->frame) return;
    const float *lim = &wk->min_x;
    for (int k = 0; k < 3; k++) {
        float mn = lim[k * 2], mx = lim[k * 2 + 1];
        if (mn == 0.0f && mx == 0.0f) continue;
        StModel *mp = pGimk->mp;
        float d = GIMK_RAD2DEG * (mp->array->ang[k] - mp->ang[k]);
        if (d <= mn || !(d < mx)) wk->rot[k] = wk->rot[k] * -1.0f;
    }
    DF_CALC_RotateModel_ZXY(pGimk->mp, wk->rot);
}

static void __gimk_InitRollModel_TexScroll(GimkSt *pGimk)
{
    PS2_FRAME(0x50);
    __gimk_roll_and_tex_st *wk = (__gimk_roll_and_tex_st *)gimk_wk_alloc(pGimk, 0x20);
    __data_roll_and_tex_st *td = (__data_roll_and_tex_st *)GIMK_TD(pGimk);
    const float d2r = GIMK_DEG2RAD;
    wk->rot[0] = d2r * td->rot_x;
    wk->rot[1] = d2r * td->rot_y;
    wk->rot[2] = d2r * td->rot_z;
    wk->rot[3] = 1.0f;
    wk->tex_s = td->tex_s;
    wk->tex_t = td->tex_t;
    /* quirk: the sign test reads the destination (not td->rot_frame), so a random start is stored raw */
    if (wk->rot_frame >= 0) wk->rot_frame = td->rot_frame;
    else wk->rot_frame = gimk_abs(td->rot_frame) > 0 ? gimk_rand_range(gimk_abs(td->rot_frame)) : 0;
    wk->tex_frame = gimk_start_frame(td->tex_frame);
    gimk_add_exec(pGimk, GFN(__gimk_ExecRollModel_TexScroll), 0);
}

static void __gimk_ExecRollModel_TexScroll(GimkSt *pGimk)
{
    __gimk_roll_and_tex_st *wk = (__gimk_roll_and_tex_st *)pGimk->wk;
    float fs = ps2_div(1.0f, (float)gimk_abs(wk->tex_s));
    float ft = ps2_div(1.0f, (float)gimk_abs(wk->tex_t));
    if (!(FRAME < (unsigned int)wk->rot_frame)) DF_CALC_RotateModel_ZXY(pGimk->mp, wk->rot);
    if (FRAME < (unsigned int)wk->tex_frame) return;
    gimk_scroll(&pGimk->mp->vuparam.addST[0], fs, wk->tex_s);
    gimk_scroll(&pGimk->mp->vuparam.addST[1], ft, wk->tex_t);
}

/* ---- sfx: an effect on the model, or on its position relative to a parent model ---- */

static void __gimk_InitSfx(GimkSt *pGimk)
{
    PS2_FRAME(0x40);
    __gimk_sfx_st *wk = (__gimk_sfx_st *)gimk_wk_alloc(pGimk, 0x10);
    __data_sfx_st *td = (__data_sfx_st *)GIMK_TD(pGimk);
    wk->run = 0;
    wk->m_or_s = td->m_or_s != 0;
    wk->layer = td->layer != 0;
    wk->parent = td->parent & 3;
    wk->scale = 0;
    wk->alpha = 0;
    wk->type = td->type;
    wk->sfx_id = td->sfx_id;
    wk->aid = td->aid;
    wk->dying_cnt = 0;
    wk->hSfx = 0;
    wk->frame = gimk_start_frame(td->frame);
    gimk_add_exec(pGimk, GFN(__gimk_ExecSfx), 0);
}

/* the start of every sfx exec: scale / alpha follow the model, a running effect dies with its layer, a new one
 * waits for the layer and every `frame` frames. 1 = go on and spawn. */
static int gimk_sfx_gate(GimkSt *pGimk, __gimk_sfx_st *wk)
{
    StModel *mp = pGimk->mp;
    if (!wk->m_or_s && wk->run) {
        if (wk->scale) DF_MAGIC2_SetScaleRate(wk->hSfx, mp->scale[0]);
        if (wk->alpha) DF_MAGIC2_SetAlphaRate(wk->hSfx, pGimk->mp->vuparam.mulalpha);
    }
    if (wk->run) {
        if (wk->hSfx && wk->layer && !pGimk->mp->on_layer) gimk_sfx_kill(wk);
        return 0;
    }
    if (wk->layer && !pGimk->mp->on_layer) return 0;
    if (wk->frame && FRAME % (unsigned int)wk->frame) return 0;
    return 1;
}

static void __gimk_ExecSfxNormal(GimkSt *pGimk)
{
    PS2_FRAME(0x50);
    __gimk_sfx_st *wk = (__gimk_sfx_st *)pGimk->wk;
    if (!gimk_sfx_gate(pGimk, wk)) return;
    StModel *mp = pGimk->mp;
    if (!wk->m_or_s) wk->hSfx = DF_MAGIC2_AppearMagic(wk->sfx_id, nullptr, nullptr, nullptr, nullptr, &mp->matrix2,
                                                      nullptr, 4);
    else wk->hSfx = (int)PS2PTR(DF_SPELL2_AppearSpell((short)wk->sfx_id, 0, 0, nullptr, nullptr, mp->loc, mp->ang,
                                                      nullptr, nullptr, nullptr, 0, 0));
    if (wk->hSfx) wk->run = 1;
}

/* effect at the model's offset from the parent model's array position: magic in the parent's frame (for a parent
 * of type 3 the offset is turned back by the parent's array rotation and the effect follows its first object),
 * spell at the parent's position */
static void gimk_sfx_parent(GimkSt *pGimk, __gimk_sfx_st *wk, StModel *par)
{
    if (!gimk_sfx_gate(pGimk, wk)) return;
    PS2_STACK(float[4], p, 0xf0);
    PS2_STACK(float[4], a, 0xe0);
    memcpy(p, pGimk->mp->loc, 16);
    for (int k = 0; k < 3; k++) p[k] = p[k] - par->array->loc[k];
    p[3] = 1.0f;
    memcpy(a, pGimk->mp->ang, 16);
    for (int k = 0; k < 3; k++) a[k] = a[k] - par->array->ang[k];
    for (int k = 0; k < 3; k++) gimk_angle_wrap(&a[k]);
    a[3] = 1.0f;
    if (!wk->m_or_s) {
        if (par->type == 3) {
            PS2_STACK(float[4][4], m, 0x60);
            PS2_STACK(float[4][4], inv, 0xa0);
            sceVu0CopyMatrix(m, IdentityMtx);
            sceVu0RotMatrixZ(m, m, par->array->ang[2]);
            sceVu0RotMatrixX(m, m, par->array->ang[0]);
            sceVu0RotMatrixY(m, m, par->array->ang[1]);
            sceVu0InversMatrix(inv, m);
            sceVu0CopyMatrix(m, inv);
            vu_apply(p, (const float (*)[4])m, p);
            wk->hSfx = DF_MAGIC2_AppearMagic(wk->sfx_id, p, a, nullptr, nullptr, par->op->wrmatp, nullptr, 4);
        } else
            wk->hSfx = DF_MAGIC2_AppearMagic(wk->sfx_id, p, a, nullptr, nullptr, &par->matrix2, nullptr, 4);
    } else {
        for (int k = 0; k < 3; k++) p[k] = par->loc[k] + p[k];
        p[3] = 1.0f;
        for (int k = 0; k < 3; k++) a[k] = par->ang[k] + a[k];
        memcpy(&a[3], &par->ang[3], 4);
        for (int k = 0; k < 3; k++) gimk_angle_wrap(&a[k]);
        a[3] = 1.0f;
        wk->hSfx = (int)PS2PTR(DF_SPELL2_AppearSpell((short)wk->sfx_id, 0, 0, nullptr, nullptr, p, a, nullptr,
                                                     nullptr, nullptr, 0, 0));
    }
    if (wk->hSfx) wk->run = 1;
}

/* runs only once the parent model is dead / being destroyed (use 4 / 0x10) */
static void __gimk_ExecSfxParentDead(GimkSt *pGimk)
{
    PS2_FRAME(0x100);
    __gimk_sfx_st *wk = (__gimk_sfx_st *)pGimk->wk;
    StModel *par = STAGE_GetModelPointer(wk->type, wk->aid);
    if (!(par->use & 0x14)) return;
    gimk_sfx_parent(pGimk, wk, par);
}

/* runs while the parent lives; when it dies a magic effect fades out over 50 frames, then the gimk is killed */
static void __gimk_ExecSfxParentAlive(GimkSt *pGimk)
{
    PS2_FRAME(0x100);
    __gimk_sfx_st *wk = (__gimk_sfx_st *)pGimk->wk;
    StModel *par = STAGE_GetModelPointer(wk->type, wk->aid);
    if (par->use & 0x14) {
        if (wk->run) {
            if (!wk->m_or_s && wk->dying_cnt < 0x32) {
                DF_MAGIC2_SetAlphaRate(wk->hSfx, ps2_u2f(0x3ca3d70a) * (float)(0x32 - wk->dying_cnt));
                wk->dying_cnt++;
                return;
            }
            gimk_sfx_kill(wk);
        }
        gimk_kill(pGimk);
        return;
    }
    gimk_sfx_parent(pGimk, wk, par);
}

static void __gimk_ExecSfx(GimkSt *pGimk)
{
    PS2_FRAME(0x10);
    __gimk_sfx_st *wk = (__gimk_sfx_st *)pGimk->wk;
    if (FRAME < (unsigned int)wk->frame) return;
    if (wk->parent == 2) __gimk_ExecSfxParentDead(pGimk);
    else if (wk->parent == 1) __gimk_ExecSfxParentAlive(pGimk);
    else __gimk_ExecSfxNormal(pGimk);
}

/* ---- model attached to an object of another model ---- */

static void __gimk_InitModelAttach(GimkSt *pGimk)
{
    PS2_FRAME(0x20);
    if (pGimk->wk_empty < 8) DF_GIMK_ErrorMSG();
    pGimk->wk_empty -= 8;
    __gimk_model_atc_st *wk = (__gimk_model_atc_st *)pGimk->wk;
    __data_model_atc_st *td = (__data_model_atc_st *)GIMK_TD(pGimk);
    wk->type = td->type;
    wk->oid = td->oid;
    wk->aid = td->aid;
    if (!td->dest) {
        pGimk->mp->nowhp = 0x7fffffff;
        wk->nowhp = 0x7fffffff;
    } else
        __gimk_InitDestruct(pGimk);
    gimk_add_exec(pGimk, GFN(__gimk_ExecModelAttach), 1);
}

static void __gimk_ExecModelAttach(GimkSt *pGimk)
{
    PS2_FRAME(0x60);
    __gimk_model_atc_st *wk = (__gimk_model_atc_st *)pGimk->wk;
    __data_model_atc_st *td = (__data_model_atc_st *)GIMK_TD(pGimk);
    if (g_sortie.pwork->isPlayPolyg == 1) return;
    StModel *par = STAGE_GetModelPointer(wk->type, wk->aid);
    if ((par->use & 0x10) && !td->dest) pGimk->mp->use |= 0x10;
    if (par->use & 4) {
        if (td->dest && !((__gimk_dest_st *)&pGimk->wk[0x20])->dest_now) DF_GIMK_ModelBomb(pGimk);
        if (!td->draw) {
            pGimk->mp->use &= 0xffef;
            pGimk->mp->use |= 7;
        }
        if (!td->atc) gimk_kill(pGimk);
    }
    ObjectSt *o = &par->op[wk->oid];
    PS2_STACK(float[4], ctr, 0x50);
    gimk_set_matrix(pGimk->mp, *o->wrmatp, ctr);
}

/* damage taken by an attached model is passed on to its parent (destructible attachments run their own) */
static void __gimk_ExecModelAttachHit(GimkSt *pGimk, float *hloc, SfxDstat *dstat)
{
    PS2_FRAME(0x70);
    __gimk_model_atc_st *wk = (__gimk_model_atc_st *)pGimk->wk;
    __data_model_atc_st *td = (__data_model_atc_st *)GIMK_TD(pGimk);
    StModel *par = STAGE_GetModelPointer(wk->type, wk->aid);
    int d = wk->nowhp - pGimk->mp->nowhp;
    if (td->dest) {
        __gimk_ExecDestruct(pGimk, hloc, dstat);
        return;
    }
    wk->nowhp = pGimk->mp->nowhp;
    if (par->nowhp > 0) {
        par->nowhp -= d;
        if (par->nowhp < 0) par->nowhp = 0;
    }
    if (!DF_GIMK_IsDestroyModel(par)) return;
    GimkSt *pg = (GimkSt *)par->arg0p;
    switch (pg->type) {
    case 0x30: __gimk_ExecBuilt(pg, hloc); break;
    case 0x18: __gimk_ExecDestPG(pg, hloc, dstat); break;
    case 0x36: case 0x16: case 0x14: case 0x12: case 0x10: __gimk_ExecDestruct(pg, hloc, dstat); break;
    case 0xc: __gimk_ExecModelAttachHit(pg, hloc, dstat); break;
    }
}

/* ---- building: parts hit (children of op->child->sibling) switch to their broken twin (next sibling chain) ---- */

static void __gimk_InitBuilt(GimkSt *pGimk)
{
    PS2_FRAME(0x20);
    __gimk_buil_st *wk = (__gimk_buil_st *)gimk_wk_alloc(pGimk, 0x18);
    __data_buil_st *td = (__data_buil_st *)GIMK_TD(pGimk);
    wk->m_or_s = td->m_or_s;
    wk->sfx_id = td->sfx_id;
    wk->snd_id = td->snd_id;
    wk->snd_range = td->snd_range;
    wk->width = td->width;
    wk->height = td->height;
    wk->depth = td->depth;
    wk->frame = 0;
    ObjectSt *o = pGimk->mp->op->child->sibling->sibling;
    o->onoff = 1;
    for (o = o->child; o; o = o->sibling) o->onoff = 1;
}

static inline float gimk_fabs(float f) { return f < 0.0f ? -f : f; }

static void __gimk_ExecBuilt(GimkSt *pGimk, float *hloc)
{
    PS2_FRAME(0xd0);
    __gimk_buil_st *wk = (__gimk_buil_st *)pGimk->wk;
    ObjectSt *o1 = pGimk->mp->op->child->sibling;
    ObjectSt *o2 = o1->sibling;
    ObjectSt *a = o1->child, *b = o2->child;
    if (o1->onoff) return;
    PS2_STACK(float[4], pos, 0xc0);
    PS2_STACK(float[4], ang, 0xb0);   /* DFvu0_Matrix2YXZangN leaves w (stale stack) */
    PS2_STACK(float[4], h, 0xa0);
    PS2_STACK(float[4], one, 0x90);
    for (; a; a = a->sibling, b = b->sibling) {
        if (a->onoff) continue;
        DF_GIMK_GetObjectPos(a, pos);
        DF_GIMK_GetHitPos(a, hloc, pos, h);
        if (!(gimk_fabs(h[0]) < wk->width / 2.0f)) continue;
        if (!(gimk_fabs(h[1]) < wk->height / 2.0f)) continue;
        if (!(gimk_fabs(h[2]) < wk->depth / 2.0f)) continue;
        a->onoff = 1;
        b->onoff = 0;
        DFvu0_Matrix2YXZangN(ang, *a->wrmatp);
        if (!wk->m_or_s) {
            one[0] = one[1] = one[2] = one[3] = 1.0f;
            DF_MAGIC2_AppearMagic(wk->sfx_id, pos, ang, one, nullptr, nullptr, nullptr, 0);
        } else
            DF_SPELL2_AppearSpell((short)wk->sfx_id, 0, 0, nullptr, nullptr, pos, ang, nullptr, nullptr, nullptr, 0, 0);
        if ((unsigned int)(wk->frame + 10) < FRAME) {
            Voice_CallPoint(wk->snd_id, 0x3c007f, (float (*)[4])pos, (float)wk->snd_range);
            wk->frame = FRAME;
        }
    }
}

/* ---- destructible models (types 0x10-0x16, also the car and destructible attachments): the destruction data
 * sits at typedata + 0x10, the state at GimkSt + 0x30 ---- */

#define GIMK_DEST(g) ((__gimk_dest_st *)&(g)->wk[0x20])
#define GIMK_DEST_TD(g) ((__data_dest_st *)(GIMK_TD(g) + 0x10))

/* stage script (>= 1000) or event-manager command started by a destruction */
static void gimk_script(unsigned int id, StModel *mp)
{
    if (!id) return;
    if ((int)id >= 1000) DF_ScriptStart(id, mp);
    else EVMNG_Entry(id);
}

/* the separate broken model (aid + 1) takes the intact one's place */
static void gimk_show_after_model(GimkSt *pGimk, StModel *m)
{
    DF_GIMK_SetModelMatrix(m, pGimk->mp->matrix2);
    m->groupid = pGimk->mp->groupid;
}

static void __gimk_InitDestruct(GimkSt *pGimk)
{
    PS2_FRAME(0x50);
    if (pGimk->wk_empty < 0x10) DF_GIMK_ErrorMSG();
    pGimk->wk_empty -= 0x10;
    __data_dest_st *dd = GIMK_DEST_TD(pGimk);
    __gimk_dest_st *d = GIMK_DEST(pGimk);
    pGimk->mp->nowhp = dd->hp;
    d->dest_now = 0;
    d->after_dest = dd->after_dest;
    d->after_sfx = dd->after_sfx;
    d->touch_dest = dd->ride >= 2;
    d->lead_type = dd->lead_type;
    d->lead_frame = dd->lead_frame;
    d->lead_dist = (float)dd->lead_dist;
    d->bomb_frame = 0x7fffffff;
    if (dd->lead) pGimk->gflag |= 1;
    if (dd->ride) pGimk->gflag |= 2;
    if (dd->script_id) d->script_id = dd->script_id;
    if (Gfunc_IsNowDestroyModel(pGimk)) {
        Gfunc_AfterDestroySetting(pGimk);
        return;
    }
    if (d->after_dest) {
        StModel *m = STAGE_GetModelPointer(pGimk->mp->type, pGimk->mp->aid + 1);
        m->use |= 3;
        m->mflag |= 0x80;
    } else {
        ObjectSt *o = DF_GIMK_GetDestObject(pGimk->mp);
        if (o) DF_GIMK_SetDrawObject(o, 0);
    }
    if (!pGimk->mp->nowhp) pGimk->mp->nowhp = 1;
    if (d->touch_dest) gimk_add_exec(pGimk, GFN(__gimk_ExecTouchDestruct), 1);
    ArraySt *arr = pGimk->mp->array;
    if (arr->belonging && arr->noiselv > 0)
        pGimk->ecmhandle = EcmField_RegisterEcm(2, arr->belonging, 0, arr->noiselv);
    unsigned int t = pGimk->type;
    if (t != 0x12 && t != 0x16 && t != 0x36) return;
    /* animated destructible: the idle animation runs until destruction (anim_frame = start frame / random start) */
    Anim_SetAnimId(pGimk->mp, dd->anim_id);
    Anim_SetLoop(pGimk->mp, 1);
    d->anim_run = 1;
    StXtod *tod = pGimk->mp->tod;
    if (!((unsigned int)gimk_abs(dd->anim_frame) < tod->head[dd->anim_id]->frame))
        d->anim_frame = gimk_start_frame(dd->anim_frame);
    else {
        pGimk->mp->tod->nowframe = gimk_start_frame(dd->anim_frame);
        d->anim_frame = 0;
    }
    gimk_add_exec(pGimk, GFN(__gimk_ExecAnimDestruct), 0);
}

/* a hit that leaves the model at 0 hp: too weak a hit (damage / heat below the type's thresholds) keeps it at 1 hp,
 * otherwise the destruction animation starts */
static void __gimk_ExecDestruct(GimkSt *pGimk, float *hloc, SfxDstat *pDstat)
{
    PS2_FRAME(0x30);
    (void)hloc;
    __gimk_dest_st *d = GIMK_DEST(pGimk);
    StModel *mp = pGimk->mp;
    if (mp->use & 4) return;
    if (mp->nowhp) return;
    if (d->dest_now) return;
    if (pDstat) {
        __data_dest_st *dd = (__data_dest_st *)(mp->array->typedata + 0x10);
        if ((int)pDstat->damage < s_DestDmg[dd->destDmgID]) {
            mp->nowhp = 1;
            return;
        }
        if ((int)pDstat->heat < s_DestHeat[dd->destHeatID]) {
            mp->nowhp = 1;
            return;
        }
    }
    mp->tod->nowframe = 0;
    if (!gimk_add_exec(pGimk, GFN(__gimk_DestructModel), 0)) {
        pGimk->mp->nowhp = 1;
        return;
    }
    d->dest_now = 1;
    if (d->anim_run) d->anim_run = 0;
    __gimk_BeginDestruct(pGimk);
}

static void __gimk_BeginDestruct(GimkSt *pGimk)
{
    PS2_FRAME(0x40);
    __gimk_dest_st *d = GIMK_DEST(pGimk);
    if (d->after_dest && d->after_sfx) {
        StModel *m = STAGE_GetModelPointer(pGimk->mp->type, pGimk->mp->aid + 1);
        gimk_show_after_model(pGimk, m);
        Anim_SetAnimId(m, 0);
        Anim_SetLoop(m, 0);
        Anim_SetSfx(m, 1);
        m->use &= 0xfffc;
        if (!(m->array->mflag & 0x80)) m->mflag &= 0xff7f;   /* quirk: andi also clears bits 16-31 */
        pGimk->mp->use |= 3;
        pGimk->mp->mflag |= 0x80;
    } else {
        Anim_SetAnimId(pGimk->mp, 0);
        Anim_SetLoop(pGimk->mp, 0);
        Anim_SetSfx(pGimk->mp, 1);
        if (d->after_sfx) {
            ObjectSt *o = DF_GIMK_GetDestObject(pGimk->mp);
            if (o) DF_GIMK_SetDrawObject(o, 1);
            o = DF_GIMK_GetDrawObject(pGimk->mp);
            if (o) {
                o->onoff = 1;
                if (o->child) DF_GIMK_SetDrawObject(o->child, 0);
                pGimk->mp->use |= 2;
                pGimk->mp->mflag |= 0x80;
            }
        }
    }
    pGimk->mp->use |= 0x10;
    __gimk_SeekLeadBomb(pGimk);
    DF_GIMK_ObjDestroy(pGimk);
    gimk_script(d->script_id, pGimk->mp);
}

static void __gimk_EndDestruct(GimkSt *pGimk)
{
    PS2_FRAME(0x30);
    __gimk_dest_st *d = GIMK_DEST(pGimk);
    if (d->after_dest && !d->after_sfx) {
        StModel *m = STAGE_GetModelPointer(pGimk->mp->type, pGimk->mp->aid + 1);
        gimk_show_after_model(pGimk, m);
        m->use &= 0xfffc;
        if (!(m->array->mflag & 0x80)) m->mflag &= 0xff7f;
        pGimk->mp->use |= 3;
        pGimk->mp->mflag |= 0x80;
    } else if (!d->after_sfx) {
        ObjectSt *o = DF_GIMK_GetDestObject(pGimk->mp);
        if (o) {
            DF_GIMK_SetDrawObject(o, 1);
            pGimk->mp->vuparam.mulalpha = 1.0f;
        } else
            pGimk->mp->use |= 1;
        o = DF_GIMK_GetDrawObject(pGimk->mp);
        if (o) {
            o->onoff = 1;
            if (o->child) DF_GIMK_SetDrawObject(o->child, 0);
            pGimk->mp->use |= 2;
            pGimk->mp->mflag |= 0x80;
        }
    }
    pGimk->mp->use &= 0xffef;
    pGimk->mp->use |= 4;
    if (pGimk->ecmhandle) EcmField_UnRegisterEcm(pGimk->ecmhandle);
}

/* exec callback while destroying: runs the destruction animation (fading with object 1's y as alpha) */
static void __gimk_DestructModel(GimkSt *pGimk)
{
    PS2_FRAME(0x40);
    __gimk_dest_st *d = GIMK_DEST(pGimk);
    StModel *m;
    if (d->after_dest && d->after_sfx) m = STAGE_GetModelPointer(pGimk->mp->type, pGimk->mp->aid + 1);
    else m = pGimk->mp;
    if (Anim_IsEndTod(m) == 1) {
        __gimk_EndDestruct(pGimk);
        gimk_kill(pGimk);
    } else
        Anim_AnimMi2TodUpdate(m);
    if (m->mflag & 0x40) {
        float f = m->op[1].position[1];
        if (f < ps2_u2f(0x3f7d70a4)) m->vuparam.mulalpha = f;
    }
}

static void __gimk_ExecAnimDestruct(GimkSt *pGimk)
{
    PS2_FRAME(0x10);
    __gimk_dest_st *d = GIMK_DEST(pGimk);
    if (!d->anim_run) return;
    if (FRAME < d->anim_frame) return;
    gimk_anim_step(pGimk->mp);
}

/* destroyed by an AC (model type 4) or a constant-move model standing on it */
static void __gimk_ExecTouchDestruct(GimkSt *pGimk)
{
    PS2_FRAME(0x20);
    StModel *mp = pGimk->mp;
    if (mp->use & 0x10 || mp->use & 4 || !mp->nowhp || pGimk->sflag) return;
    StModel *m = DF_GIMK_GetOnAC(pGimk);
    if (!m) return;
    if (m->type == 4) DF_GIMK_ModelBomb(pGimk);
    if (DF_GIMK_GetOnCstMT(pGimk)) DF_GIMK_ModelBomb(pGimk);
}

/* a lead-bomb target that can still be chained */
static int gimk_lead_ok(GimkSt *t, GimkSt *pGimk)
{
    if (!t) return 0;
    if (!(t->gflag & 1)) return 0;
    StModel *m = t->mp;
    if (m->use & 0x10 || m->use & 4 || !m->nowhp || t->sflag) return 0;
    if (t == pGimk) return 0;
    return !GIMK_DEST(t)->dest_now;
}

/* t blows up lead_frame frames from now, credited to the same shooter */
static void gimk_lead_chain(GimkSt *t, GimkSt *pGimk, __data_dest_st *dd)
{
    GIMK_DEST(t)->bomb_frame = FRAME + dd->lead_frame;
    t->mp->shooter = pGimk->mp->shooter;
    t->mp->shooterid = pGimk->mp->shooterid;
    gimk_add_exec(t, GFN(__gimk_ExecLeadBomb), 0);
}

/* chain reaction: lead type 6 = the next lead_dist models (after the broken twin), else every lead target within
 * lead_dist of the bounding-sphere centre, filtered by type (1 any, 2 |dy| <= 1, 3 below, 4 above, 5 |dx|, |dz| <= 1) */
static void __gimk_SeekLeadBomb(GimkSt *pGimk)
{
    PS2_FRAME(0x90);
    __gimk_dest_st *d = GIMK_DEST(pGimk);
    StModel *m = pGimk->mp;
    __data_dest_st *dd = (__data_dest_st *)(m->array->typedata + 0x10);
    if (d->lead_type == 6) {
        int n = dd->lead_dist;
        if (d->after_dest) m++;
        for (int i = 0; i < n; i++) {
            m++;
            GimkSt *t = (GimkSt *)m->arg0p;
            if (!t) continue;
            if (!DF_GIMK_IsDestroyModel(m)) continue;
            if (!DF_GIMK_IsDestroyModel(m)) continue;   /* called twice */
            if (!gimk_lead_ok(t, pGimk)) continue;
            gimk_lead_chain(t, pGimk, dd);
        }
        return;
    }
    PS2_STACK(float[4], v, 0x80);
    for (int i = 0; i < s_GimkNum; i++) {
        GimkSt *t = &s_pGimk[i];
        float r = d->lead_dist;
        if (!gimk_lead_ok(t, pGimk)) continue;
        const float *a = t->mp->mlist->sphere, *b = pGimk->mp->mlist->sphere;
        memcpy(v, a, 16);
        for (int k = 0; k < 3; k++) v[k] = a[k] - b[k];
        float rr = r * r;
        float zz = v[2] * v[2], xx = v[0] * v[0], yy = v[1] * v[1];
        if (!(zz + (xx + yy) <= rr)) continue;
        int go;
        switch (d->lead_type) {
        case 1: go = 1; break;
        case 2: go = gimk_fabs(v[1]) <= 1.0f; break;
        case 3: go = v[1] <= ps2_u2f(0xbdcccccd); break;
        case 4: go = !(v[1] < ps2_u2f(0x3dcccccd)); break;
        case 5: go = gimk_fabs(v[0]) <= 1.0f && gimk_fabs(v[2]) <= 1.0f; break;
        default: go = 0; break;
        }
        if (go) gimk_lead_chain(t, pGimk, dd);
    }
}

/* the chained model blows up at bomb_frame (the node stays in the list) */
static void __gimk_ExecLeadBomb(GimkSt *pGimk)
{
    PS2_FRAME(0x10);
    StModel *m = pGimk->mp;
    if (m->use & 0x10 || m->use & 4 || !m->nowhp || pGimk->sflag) return;
    if (FRAME < (unsigned int)GIMK_DEST(pGimk)->bomb_frame) return;
    if (GIMK_DEST(pGimk)->dest_now) return;
    DF_GIMK_ModelBomb(pGimk);
}

/* ---- elevator (wk struct_5b2f7e9e: state 0 idle, 1 / 2 start forward / back, 3 / 4 moving; pos 0 bottom, 1 top,
 * 2 moving) and its call switch ---- */

/* door / model reference of the elevator data: model type in bits 12-15, aid in bits 0-11 */
static StModel *gimk_elv_ref(unsigned int v)
{
    return STAGE_GetModelPointer((int)(v & 0xf000) >> 12, v & 0xfff);
}

/* light of a referenced door (its typedata[1]) to light data s */
static void gimk_elv_light(unsigned int v, int s)
{
    if (!v) return;
    StModel *m = gimk_elv_ref(v);
    if (m->array->typedata[1]) DF_LIT_Set((unsigned char)m->array->typedata[1], s);
}

/* the elevator gimk an event id names (the switch's elv_evid) */
static struct_5b2f7e9e *gimk_elv_by_event(int evid)
{
    __evmng_event_id_st *ev = EVMNG_GetEventID(evid);
    StModel *m = STAGE_GetModelPointer(ev->type, ev->id);
    return (struct_5b2f7e9e *)((GimkSt *)m->arg0p)->wk;
}

static void __gimk_InitElevator(GimkSt *pGimk)
{
    PS2_FRAME(0x20);
    struct_5b2f7e9e *e = (struct_5b2f7e9e *)gimk_wk_alloc(pGimk, 0x1c);
    __data_elv_st *td = (__data_elv_st *)GIMK_TD(pGimk);
    e->state = 0;
    e->pos = 0;
    e->ride = 1;
    e->move = 1;
    e->frame = -(int)td->wait_frame;
    e->hSnd = 0;
    e->pFunc = nullptr;
    e->ride_frame = 0;
    e->pSw = nullptr;
    e->turn_wait = 0;
    pGimk->gflag |= 2;
    Anim_SetAnimId(pGimk->mp, td->anim_id);
    Anim_SetLoop(pGimk->mp, 0);
    gimk_add_exec(pGimk, GFN(__gimk_ExecElevator), 1);
}

/* late pass: starts a trip once someone has ridden 100 frames (automatic elevators also wait wait_frame after
 * arriving), unless the door at the departure end is moving; ride is cleared by msg 1 each frame someone stands on
 * it and set again here */
static void __gimk_ExecElevator(GimkSt *pGimk)
{
    PS2_FRAME(0x40);
    struct_5b2f7e9e *e = (struct_5b2f7e9e *)pGimk->wk;
    __data_elv_st *td = (__data_elv_st *)GIMK_TD(pGimk);
    if (e->pos == 2 || !e->move || e->ride_frame < 100) goto ride;
    if (!td->sw && FRAME < (unsigned int)(e->frame + td->wait_frame)) goto ride;
    {
        int t = e->turn_wait;
        e->turn_wait = t - 1;
        if (t > 0) return;
    }
    if (!td->sw && !e->state) {
        if (!e->pos) e->state = 1;
        else if (e->pos == 1) e->state = 2;
    }
    if (e->ride || e->pos == 2) goto ride;
    if (e->state == 1 || e->state == 2) {
        int fw = e->state == 1;
        unsigned int door = fw ? td->fw_door : td->rv_door;
        if (door) {
            int st = (signed char)DOOR_GetDoorState(gimk_elv_ref(door));
            if (st && st != 4) {
                e->ride_frame = 0;
                goto ride;
            }
        }
        e->pFunc = gimk_add_exec(pGimk, fw ? GFN(__gimk_ExecElevatorFW) : GFN(__gimk_ExecElevatorRV), 0);
        e->state = fw ? 3 : 4;
        e->pos = 2;
        e->turn_wait = 0;
        gimk_elv_light(td->fw_door, 2);
        gimk_elv_light(td->rv_door, 2);
    }
ride:
    e->ride = 1;
}

/* no AC standing on the departure end's dummy points (fw_dmy / rv_dmy) */
static int __gimk_IsElevatorSafePos(GimkSt *pGimk)
{
    PS2_FRAME(0x20);
    int dmy[2] = {0, 0};
    struct_5b2f7e9e *e = (struct_5b2f7e9e *)pGimk->wk;
    __data_elv_st *td = (__data_elv_st *)GIMK_TD(pGimk);
    if (e->state == 3) {
        dmy[0] = td->fw_dmy[0];
        dmy[1] = td->fw_dmy[1];
    } else if (e->state == 4) {
        dmy[0] = td->rv_dmy[0];
        dmy[1] = td->rv_dmy[1];
    }
    if (dmy[0] && AcCtrl_IsArriveDmy(0, dmy[0], 10)) return 0;
    if (dmy[1] && AcCtrl_IsArriveDmy(0, dmy[1], 10)) return 0;
    return 1;
}

/* one trip frame (fw: forward animation to the top, else reverse to the bottom); stops (pos back to the start,
 * 60 frames turn wait) when an AC blocks the far end, pauses (pos 2) while nobody rides; carries the carry models */
static void gimk_elv_trip(GimkSt *pGimk, int fw)
{
    PS2_FRAME(0x50);
    struct_5b2f7e9e *e = (struct_5b2f7e9e *)pGimk->wk;
    __data_elv_st *td = (__data_elv_st *)GIMK_TD(pGimk);
    int ok = 1;
    if ((td->safe & (fw ? 1 : 2)) && !__gimk_IsElevatorSafePos(pGimk)) {
        if (e->hSnd) {
            Voice_Stop(e->hSnd, 0);
            e->hSnd = 0;
        }
        Voice_CallPoint(td->point_snd, 0x3c007f, (float (*)[4])pGimk->mp->loc, (float)td->snd_dist);
        ((GimkFuncListSt *)e->pFunc)->fflag |= 0x80;
        e->pFunc = nullptr;
        e->state = 0;
        e->pos = fw ? 1 : 0;
        e->ride = 0;
        e->turn_wait = 0x3c;
        return;
    }
    if (e->ride_frame < 100) ok = 0;
    if (!e->move) ok = 0;
    if (!ok) {
        if (e->hSnd) {
            Voice_Stop(e->hSnd, 0);
            Voice_CallPoint(td->point_snd, 0x3c007f, (float (*)[4])pGimk->mp->loc, (float)td->snd_dist);
            e->hSnd = 0;
        }
        e->pos = 2;
        return;
    }
    if (!e->hSnd || e->hSnd >= 0x51)
        e->hSnd = Voice_CallTraceLoc(td->loop_snd, 0x3c007f, (float (*)[4])pGimk->mp->loc, (float)td->snd_dist);
    if (fw) Anim_AnimMi2TodUpdateN(pGimk->mp);
    else Anim_AnimMi2TodUpdateReverseN(pGimk->mp);
    if (td->carry)
        for (int i = 0; i < 8 && td->carry_id[i]; i++) {
            __evmng_event_id_st *ev = EVMNG_GetEventID(td->carry_id[i]);
            DF_GIMK_ModelAttach(pGimk->mp, STAGE_GetModelPointer(ev->type, ev->id));
        }
    e->state = fw ? 3 : 4;
    e->pos = 2;
    if (!DF_GIMK_AnimEnd(pGimk->mp, fw ? 0 : 1)) return;
    /* arrived */
    if (e->hSnd) {
        Voice_Stop(e->hSnd, 0);
        e->hSnd = 0;
    }
    Voice_CallPoint(td->point_snd, 0x3c007f, (float (*)[4])pGimk->mp->loc, (float)td->snd_dist);
    e->state = 0;
    e->pos = fw ? 1 : 0;
    e->frame = FRAME;
    e->ride_frame = 0;
    gimk_elv_light(td->fw_door, 1);
    gimk_elv_light(td->rv_door, 1);
    if (!fw) DF_MDL_SetModel(pGimk->mp, pGimk->mp->array->loc, pGimk->mp->array->ang, nullptr);
    ((GimkFuncListSt *)e->pFunc)->fflag |= 0x80;
    e->pFunc = nullptr;
    if (td->sw && e->pSw) {
        GimkSt *sw = e->pSw;
        ((__gimk_elv_sw_st *)sw->wk)->return_wait = FRAME + ((__data_elv_sw_st *)GIMK_TD(sw))->return_wait;
    }
}

static void __gimk_ExecElevatorFW(GimkSt *pGimk) { gimk_elv_trip(pGimk, 1); }
static void __gimk_ExecElevatorRV(GimkSt *pGimk) { gimk_elv_trip(pGimk, 0); }

static void __gimk_InitElevatorSwitch(GimkSt *pGimk)
{
    PS2_FRAME(0x20);
    __gimk_elv_sw_st *s = (__gimk_elv_sw_st *)gimk_wk_alloc(pGimk, 0xc);
    s->state = 1;
    s->ac_onoff = 1;
    s->move_frame = 0;
    gimk_add_exec(pGimk, GFN(__gimk_ExecElevatorSwitchState), 0);
}

/* event button on the switch: calls the elevator (only inside the useful area, if the stage info says so). kind
 * bit 1 / 0 = send it up / down: the player's own trip (ride_frame 100 = go at once, the AC is frozen ac_wait
 * frames) or just calling it over. */
static int __gimk_ExecElevatorSwitch(GimkSt *pGimk)
{
    PS2_FRAME(0x60);
    __gimk_elv_sw_st *s = (__gimk_elv_sw_st *)pGimk->wk;
    __data_elv_sw_st *td = (__data_elv_sw_st *)GIMK_TD(pGimk);
    struct_5b2f7e9e *e = gimk_elv_by_event(td->elv_evid);
    if (td->useful_area) {
        PS2_STACK(union_9a8d3bef, r, 0x5c);
        gcall_sret(GFN(Info_GetData), &r, 1, 0xd, 0, (int)td->useful_area);
        if (!r.i) return 0;
    }
    e->pSw = pGimk;
    float (*loc)[4] = (float (*)[4])pGimk->mp->loc;
    if (e->state) {
        Voice_CallPoint(td->err_snd_id, 0x3c007f, loc, 200.0f);
        return 0;
    }
    if (FRAME < s->return_wait) return 0;
    int ride, call, st;
    if (e->pos == 0) {
        ride = td->kind & 2;
        call = td->kind & 1;
        st = 1;
    } else if (e->pos == 1) {
        ride = td->kind & 1;
        call = td->kind & 2;
        st = 2;
    } else
        return 0;
    if (ride) {
        e->state = st;
        e->ride = 0;
        e->ride_frame = 100;
        e->turn_wait = 0;
        Voice_CallPoint(td->ok_snd_id, 0x3c007f, (float (*)[4])pGimk->mp->loc, 200.0f);
        s->move_frame = FRAME + td->ac_wait;
    } else if (call) {
        e->state = st;
        e->ride_frame = 0;
        e->turn_wait = 0;
        Voice_CallPoint(td->ok_snd_id, 0x3c007f, (float (*)[4])pGimk->mp->loc, 200.0f);
    }
    return 0;
}

/* switch light (3 busy, 4 ready) follows the elevator; the player's AC is frozen until move_frame */
static void __gimk_ExecElevatorSwitchState(GimkSt *pGimk)
{
    PS2_FRAME(0x40);
    __gimk_elv_sw_st *s = (__gimk_elv_sw_st *)pGimk->wk;
    __data_elv_sw_st *td = (__data_elv_sw_st *)GIMK_TD(pGimk);
    struct_5b2f7e9e *e = gimk_elv_by_event(td->elv_evid);
    if (pGimk->sflag == 2) return;
    if (s->state == 1) {
        if (e->state) {
            DF_LIT_Set(td->lit_id, 3);
            s->state = 0;
        }
    } else if (s->state == 0 && !e->state && e->pos != 2) {
        DF_LIT_Set(td->lit_id, 4);
        s->state = 1;
    }
    if (!(FRAME < (unsigned int)s->move_frame)) {
        if (!s->ac_onoff) __gimk_ElevatorSwitchAcOnOff(s, 1);
    } else if (s->ac_onoff == 1)
        __gimk_ElevatorSwitchAcOnOff(s, 0);
}

/* player AC control on / off (only with the gauge shown, i.e. in play) */
static void __gimk_ElevatorSwitchAcOnOff(__gimk_elv_sw_st *pSw, int bOnOff)
{
    PS2_FRAME(0x30);
    AcPlayerSt *ac = &g_ac[0];
    if (bOnOff) {
        if (MyI.gauge) {
            ac->stat &= 0xbfff;
            MyI.padoff = 0;
            AC_SetControl(ac, 1);
        }
        pSw->ac_onoff = 1;
    } else {
        if (MyI.gauge) {
            AC_ClearKeybuff(ac);
            ac->stat |= 0x4000;
            MyI.padoff = 1;
            AC_SetControl(ac, 0);
            AC_SetPosition(ac, ac->mp->loc);
        }
        pSw->ac_onoff = 0;
    }
}

/* ---- supply truck (car): drives along its animation while the way is clear, refills the player's AC on the
 * event button (supply_cnt times, < 0 = unlimited); destructible like type 0x36 destructibles ---- */

static void __gimk_InitCar(GimkSt *pGimk)
{
    PS2_FRAME(0x20);
    __gimk_car_st *c = (__gimk_car_st *)gimk_wk_alloc(pGimk, 0x14);
    c->nowhp = -1;
    c->supply_on = 0;
    c->move_on = 1;
    c->move_force = 0;
    c->supply_cnt = 0;
    c->supply_frame = 0;
    c->hSnd = 0;
    c->hSupplySnd = 0;
    c->safe_frame = 0;
    __gimk_InitDestruct(pGimk);
    pGimk->gflag |= 2;
    gimk_add_exec(pGimk, GFN(__gimk_ExecCar), 0);
}

static void __gimk_ExecCar(GimkSt *pGimk)
{
    PS2_FRAME(0x60);
    __gimk_car_st *c = (__gimk_car_st *)pGimk->wk;
    __gimk_dest_st *d = GIMK_DEST(pGimk);
    __data_car_st *td = (__data_car_st *)GIMK_TD(pGimk);
    if (pGimk->mp->use & 4) return;
    __gimk_CarSupply(pGimk);
    int go = 1;
    if (c->supply_on) go = 0;
    if (!__gimk_IsCarSafePos(pGimk, td)) go = 0;   /* always called: counts safe_frame */
    StModel *mp = pGimk->mp;
    if (mp->use & 0x10) go = 0;
    if (!c->move_on) go = 0;
    if (c->move_force) go = 1;
    if (go) {
        d->anim_run = 1;
        if ((!c->hSnd || c->hSnd >= 0x51) && td->snd_id)
            c->hSnd = Voice_CallTraceLoc(td->snd_id, 0x3c007f, (float (*)[4])pGimk->mp->loc, (float)td->snd_dist);
    } else {
        mp->groupid = g_ac[0].mp->nowparent->groupid;
        pGimk->mp->on_layer = 3;
        d->anim_run = 0;
        if (c->hSnd && c->hSnd < 0x51) {
            Voice_Stop(c->hSnd, 0);
            c->hSnd = 0;
        }
    }
    c->ride_on = 0;
}

/* refill in progress: freezes the player's AC, restores ammo / AP every frame for supply_frame frames (the truck
 * is invulnerable meanwhile: nowhp -1) */
static void __gimk_CarSupply(GimkSt *pGimk)
{
    PS2_FRAME(0x50);
    __gimk_car_st *c = (__gimk_car_st *)pGimk->wk;
    __data_car_st *td = (__data_car_st *)GIMK_TD(pGimk);
    AcPlayerSt *ac = &g_ac[0];
    if (!c->supply_on) return;
    if (!c->supply_frame) {
        AC_ClearKeybuff(ac);
        ac->stat |= 0x4000;
        MyI.padoff = 1;
        AC_SetControl(ac, 0);
        c->nowhp = pGimk->mp->nowhp;
        pGimk->mp->nowhp = -1;
        if (!MsgWnd_IsBusy(1)) MsgWnd_ShowArb((char *)&Array->message.onemes[61], 0x150b);
        c->hSupplySnd = Voice_CallTraceLoc(0xbc3, 0x3c007f, (float (*)[4])pGimk->mp->loc, (float)td->snd_dist);
    }
    AcCtrl_MagazineSupply(ac);
    AcCtrl_APRestore(ac);
    if (c->supply_frame < td->supply_frame) {
        c->supply_frame++;
        return;
    }
    ac->stat &= 0xbfff;
    MyI.padoff = 0;
    AC_SetControl(ac, 1);
    pGimk->mp->nowhp = c->nowhp;
    if (!MsgWnd_IsBusy(1)) MsgWnd_ShowArb((char *)&Array->message.onemes[62], 0x150c);
    Voice_Stop(c->hSupplySnd, 0);
    c->hSupplySnd = 0;
    Voice_CallPoint(0xbc4, 0x3c007f, (float (*)[4])pGimk->mp->loc, (float)td->snd_dist);
    c->supply_on = 0;
    c->supply_frame = 0;
    c->supply_cnt++;
}

/* event button on the truck: starts a refill (not during the mission result, not while destroyed) */
static int __gimk_ExecCarEvent(GimkSt *pGimk)
{
    PS2_FRAME(0x30);
    __gimk_car_st *c = (__gimk_car_st *)pGimk->wk;
    __data_car_st *td = (__data_car_st *)GIMK_TD(pGimk);
    if (g_sortie.pwork->statMission) return -1;
    if (pGimk->mp->use & 0x10) return -1;
    int n = td->supply_cnt;
    if (!n) return -1;
    if (c->supply_on) return -1;
    if (n > 0 && !(c->supply_cnt < n)) {
        /* used up */
        if (!MsgWnd_IsBusy(1)) MsgWnd_ShowArb((char *)&Array->message.onemes[63], 0x150d);
        Voice_CallPoint(0xbc5, 0x3c007f, (float (*)[4])pGimk->mp->loc, (float)td->snd_dist);
        return 0;
    }
    c->supply_on = 1;
    return 0;
}

/* the truck waits safe_frame frames, then stops for map models (event ids evid_first..evid_last), enemies and ACs
 * in front of it */
static int __gimk_IsCarSafePos(GimkSt *pGimk, __data_car_st *pData)
{
    PS2_FRAME(0x50);
    __gimk_car_st *c = (__gimk_car_st *)pGimk->wk;
    if (c->safe_frame < pData->safe_frame) {
        c->safe_frame++;
        return 1;
    }
    if (pData->map_stop)
        for (int i = pData->evid_first; !(pData->evid_last < i); i++) {
            __evmng_event_id_st *ev = EVMNG_GetEventID(i);
            if (!ev) continue;
            if (!ev->type && !ev->id) continue;
            StModel *m = STAGE_GetModelPointer(ev->type, ev->id);
            if (m && !__gimk_IsCarSafePosModel(m, pGimk, pData)) return 0;
        }
    if (pData->chr_stop) {
        EnemySt *e = DFene_GetEnemy(0);
        for (int i = 0; i < EnemyNum; i++, e++) {
            if (e->use & 0x80) continue;
            if (e->mp->use & 2) continue;
            if (!__gimk_IsCarSafePosModel(e->mp, pGimk, pData)) return 0;
        }
    }
    if (pData->ac_stop)
        for (int i = 0; i < NowPlayerNum; i++)
            if (!__gimk_IsCarSafePosModel(g_ac[i].mp, pGimk, pData)) return 0;
    return 1;
}

/* 0 = pModel is within safe_ang degrees of the truck's heading and closer than safe_dist (xz) */
static int __gimk_IsCarSafePosModel(StModel *pModel, GimkSt *pGimk, __data_car_st *pData)
{
    PS2_FRAME(0x90);
    float a = pGimk->mp->ang[1];
    if (pModel == pGimk->mp) return 1;
    if (pModel->use & 4) return 1;
    if (pModel->use & 2) return 1;
    PS2_STACK(float[4], fwd, 0x60);
    PS2_STACK(float[4], d, 0x70);
    PS2_STACK(float[4], dd, 0x50);
    fwd[0] = -fsmath_SinVU0(a);
    fwd[1] = 0.0f;
    fwd[2] = -fsmath_CosVU0(a);
    fwd[3] = 1.0f;
    memcpy(d, pModel->loc, 16);
    for (int k = 0; k < 3; k++) d[k] = pModel->loc[k] - pGimk->mp->loc[k];
    memcpy(dd, d, 16);
    float q = vu_rcp_len3(d);
    for (int k = 0; k < 3; k++) d[k] = d[k] * q;
    d[3] = 0.0f;
    float c = fsmath_AcosVU0(InnerProduct(fwd, d));
    if (!(c <= (float)pData->safe_ang * GIMK_DEG2RAD)) return 1;
    float r = dd[0] * dd[0] + dd[2] * dd[2];
    float s = (float)(pData->safe_dist * pData->safe_dist);
    return !(r < s);
}

/* ---- DestPG: destruction into physical debris. The model aid + nn (nn = preDestNum, at least 1) is the debris
 * model, whose objects 2.. fly off; models aid .. aid + nn - 1 are the intact parts, aid + nn + 1 .. the remains
 * (postDestNum). pMem holds per debris object a velocity, an angular velocity (vec4 each) and a start delay. ---- */

/* rand-based fraction in (-1, 1]: (0x7fff - (rand & 0xffff)) / 32767 */
static float gimk_frac(void)
{
    return (float)(0x7fff - (ps2_rand() & 0xffff)) / ps2_u2f(0x46fffe00);
}

static int gimk_pg_nn(__data_dest_pg_st *td)
{
    int n = td->preDestNum;
    return n > 0 ? n : 1;
}

static StModel *gimk_pg_model(GimkSt *pGimk, int i)
{
    return STAGE_GetModelPointer(pGimk->mp->type, pGimk->mp->aid + i);
}

static int __DestPGGetTransCof(int transType)
{
    return (unsigned int)transType < 5 ? s_DestPGTransCof[transType] : 0;
}

static float __DestPGGetPowerCof(int powerType)
{
    return (unsigned int)powerType < 5 ? s_DestPGPowerCof[powerType] : 0.0f;
}

static float __DestPGReflect(int elasticType)
{
    return (unsigned int)elasticType < 5 ? s_DestPGElstCof[elasticType] : 0.0f;
}

static float __DestPGGetMassCof(int massType)
{
    return (unsigned int)massType < 5 ? s_DestPGMassCof[massType] : 0.0f;
}

/* unit direction of a debris object: 0 away from the model, 1 towards it, 2 random, 3 none (0, 0, 0, 1) */
static void __DestPGGetDestVec(StModel *pMdl, ObjectSt *pObj, int destType, float *vec)
{
    PS2_FRAME(0x30);
    float d[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    switch (destType) {
    case 3:
        vec[0] = vec[1] = vec[2] = 0.0f;
        vec[3] = 1.0f;
        return;
    case 2:
        d[0] = gimk_frac();
        d[1] = gimk_frac();
        d[2] = gimk_frac();
        break;
    case 1:
        memcpy(d, pMdl->loc, 16);
        for (int k = 0; k < 3; k++) d[k] = pMdl->loc[k] - (*pObj->wrmatp)[3][k];
        break;
    case 0:
        memcpy(d, (*pObj->wrmatp)[3], 16);
        for (int k = 0; k < 3; k++) d[k] = (*pObj->wrmatp)[3][k] - pMdl->loc[k];
        break;
    }
    float q = vu_rcp_len3(d);
    for (int k = 0; k < 3; k++) vec[k] = d[k] * q;
    vec[3] = 0.0f;
}

static void __gimk_InitDestPG(GimkSt *pGimk)
{
    PS2_FRAME(0xe0);
    if (pGimk->wk_empty < 0x10) DF_GIMK_ErrorMSG();
    pGimk->wk_empty -= 0x10;
    __gimk_dest_pg_st *pg = (__gimk_dest_pg_st *)pGimk->wk;
    __data_dest_pg_st *td = (__data_dest_pg_st *)GIMK_TD(pGimk);
    int nn = gimk_pg_nn(td);
    StModel *m = gimk_pg_model(pGimk, nn);
    m->use |= 3;
    m->mflag |= 0x80;
    if (Gfunc_IsNowDestroyModel(pGimk)) {
        /* destroyed on an earlier visit: intact parts gone, debris model hidden */
        int n = td->preDestNum;
        for (int i = 0; i < n; i++) {
            m = gimk_pg_model(pGimk, i);
            m->use |= 7;
            m->mflag |= 0x80;
        }
        return;
    }
    for (int i = 1; i < td->postDestNum + 1; i++) {
        m = gimk_pg_model(pGimk, nn + i);
        m->use |= 3;
        m->mflag |= 0x80;
    }
    if (!pGimk->mp->nowhp) pGimk->mp->nowhp = 1;
    StModel *dm = gimk_pg_model(pGimk, nn);
    int size = (dm->nobj - 2) * 36;
    pg->pMem = Memm_Alloc(1, size, 0x10);
    ZeroMemory32b(pg->pMem, size / 4);
    int cnt = dm->nobj - 2;
    float (*tv)[4] = (float (*)[4])pg->pMem;
    float (*rv)[4] = tv + cnt;
    ObjectSt *o = dm->op + 2;
    PS2_STACK(float[4], dir, 0xd0);
    const float d2r = GIMK_DEG2RAD;
    for (int k = 0; k < cnt; k++, o++) {
        __DestPGGetDestVec(dm, o, td->destType, dir);
        for (int j = 0; j < 3; j++) {
            float f = gimk_frac();
            tv[k][j] = dir[j] * (f * __DestPGGetPowerCof(td->powerMovType));
            f = gimk_frac();
            rv[k][j] = d2r * (f * __DestPGGetPowerCof(td->powerRotType));
        }
    }
}

/* a hit at 0 hp starts the debris (strong enough hits only); the hit position defaults to the debris model's
 * bounding-sphere centre */
static void __gimk_ExecDestPG(GimkSt *pGimk, float *hloc, SfxDstat *pDstat)
{
    PS2_FRAME(0x80);
    __gimk_dest_pg_st *pg = (__gimk_dest_pg_st *)pGimk->wk;
    __data_dest_pg_st *td = (__data_dest_pg_st *)GIMK_TD(pGimk);
    StModel *dm = gimk_pg_model(pGimk, gimk_pg_nn(td));
    StModel *mp = pGimk->mp;
    if (mp->use & 4) return;
    if (mp->nowhp) return;
    if (dm->use & 0x10) return;
    if (dm->use & 4) return;
    if (!pg->pMem) return;
    if (pDstat && (int)pDstat->damage < td->canDestDmg) {
        mp->nowhp = 1;
        return;
    }
    if (!gimk_add_exec(pGimk, GFN(__DestPGModel), 0)) {
        pGimk->mp->nowhp = 1;
        return;
    }
    PS2_STACK(float[4], h, 0x70);
    if (!hloc) {
        memcpy(h, dm->mlist->sphere, 16);
        h[3] = 1.0f;
    } else
        memcpy(h, hloc, 16);
    __DestPGBegin(pGimk, h);
}

/* swaps the intact parts for the debris model and the remains, then gives each debris object its start delay
 * (by distance from the hit) and, for the "none" direction type, a velocity away from the hit */
static void __DestPGBegin(GimkSt *pGimk, float *hloc)
{
    PS2_FRAME(0xd0);
    __gimk_dest_pg_st *pg = (__gimk_dest_pg_st *)pGimk->wk;
    __data_dest_pg_st *td = (__data_dest_pg_st *)GIMK_TD(pGimk);
    int nn = gimk_pg_nn(td);
    StModel *dm = gimk_pg_model(pGimk, nn);
    dm->groupid = pGimk->mp->groupid;
    dm->use &= 0xfffc;
    dm->use |= 0x10;
    if (!(dm->array->mflag & 0x80)) dm->mflag &= 0xff7f;   /* quirk: andi also clears bits 16-31 */
    for (int i = 0; i < nn; i++) {
        StModel *m = gimk_pg_model(pGimk, i);
        m->use |= 7;
        m->mflag |= 0x80;
    }
    for (int i = 1; i < td->postDestNum + 1; i++) {
        StModel *m = gimk_pg_model(pGimk, nn + i);
        m->use &= 0xfffc;
        if (!(m->array->mflag & 0x80)) m->mflag &= 0xff7f;
    }
    DF_GIMK_ObjDestroy(pGimk);
    pg->beginFrame = FRAME;
    dm = gimk_pg_model(pGimk, nn);
    ObjectSt *o = dm->op + 2;
    int cnt = dm->nobj - 2;
    int *dl = (int *)((char *)pg->pMem + cnt * 32);
    int tc = __DestPGGetTransCof(td->transType);
    PS2_STACK(float[4], v, 0xc0);
    for (int k = 0; k < cnt; k++, o++) {
        const float *p = (*o->wrmatp)[3];
        memcpy(v, p, 16);
        for (int j = 0; j < 3; j++) v[j] = p[j] - hloc[j];
        float xx = v[0] * v[0], yy = v[1] * v[1], zz = v[2] * v[2];
        dl[k] = __DestPGGetTransDelay(dm, ps2_sqrt(zz + (xx + yy)), tc);
    }
    if (td->destType == 3) {
        cnt = dm->nobj - 2;
        o = dm->op + 2;
        float (*tv)[4] = (float (*)[4])pg->pMem;
        float (*rv)[4] = tv + cnt;
        const float d2r = GIMK_DEG2RAD;
        for (int k = 0; k < cnt; k++, o++) {
            const float *p = (*o->wrmatp)[3];
            memcpy(v, p, 16);
            for (int j = 0; j < 3; j++) v[j] = p[j] - hloc[j];
            float q = vu_rcp_len3(v);
            for (int j = 0; j < 3; j++) v[j] = v[j] * q;
            v[3] = 0.0f;
            for (int j = 0; j < 3; j++) {
                float pw = __DestPGGetPowerCof(td->powerMovType);
                float f = gimk_frac();
                tv[k][j] = v[j] * (f * pw);
                pw = __DestPGGetPowerCof(td->powerRotType);
                f = gimk_frac();
                rv[k][j] = d2r * (f * pw);
            }
        }
    }
    gimk_script(td->scriptID, pGimk->mp);
}

/* exec callback while the debris flies: gravity scaled by the curve type and the object's mass, loss of speed over
 * destFrame, a bounce (elasticity) once an object has passed the ground level, fade-out over the last alphaFrame
 * frames, the debris sfx / sounds; ends at destFrame */
static void __DestPGModel(GimkSt *pGimk)
{
    PS2_FRAME(0xf0);
    __gimk_dest_pg_st *pg = (__gimk_dest_pg_st *)pGimk->wk;
    __data_dest_pg_st *td = (__data_dest_pg_st *)GIMK_TD(pGimk);
    unsigned int now = FRAME;
    StModel *dm = gimk_pg_model(pGimk, gimk_pg_nn(td));
    unsigned int t = now - pg->beginFrame;
    float gc = __GetGravCof(td->gravCofType, t, td->destFrame);
    if (dm->mflag & 0x40) {
        float f = ps2_div((float)(int)(t - (td->destFrame - td->alphaFrame)), (float)td->alphaFrame);
        if (!(f <= 0.0f)) dm->vuparam.mulalpha = 1.0f - f;
    }
    int cnt = dm->nobj - 2;
    float ground = dm->loc[1] + (float)td->offsetY;
    float (*tv)[4] = (float (*)[4])pg->pMem;
    float (*rv)[4] = tv + cnt;
    int *dl = (int *)(rv + cnt);
    ObjectSt *o = dm->op + 2;
    const float one = 1.0f, zero = 0.0f;
    for (int k = 0; k < cnt; k++, o++) {
        if (t < (unsigned int)dl[k]) continue;
        float y = (*o->wrmatp)[3][1];
        unsigned int dt = t - dl[k];
        float grav = gc * ((float)dt * __DestPGGetMassCof(td->massType));
        float loss = one - ps2_div((float)dt, (float)td->destFrame);
        if (!(y <= ground)) {
            if (!__DestPGRefrect(td, &tv[k], &rv[k], loss, grav)) continue;
            o->position[1] = o->position[1] - grav;
            loss = one;
            grav = zero;
            dl[k] = t;
        }
        __DestPGObject(o, tv[k], rv[k], loss, grav);
    }
    DF_CALC_UpdateModelMatrix_ZXY(dm);
    if (dm->arg0p) {
        __DestSfxProc((GimkSt *)dm->arg0p, dm, t, 0);
        __DestSfxProc((GimkSt *)dm->arg0p, dm, t, 1);
    }
    if (!(t < td->destFrame)) __DestPGEnd(pGimk);
}

/* one debris object step: position += v * loss (+ gravity on y), angle += w * loss, wrapped to [-PI, PI] */
static void __DestPGObject(ObjectSt *pObj, float *transVel, float *rotVel, float loss, float gravity)
{
    float tr[4], ro[4];
    memcpy(tr, transVel, 16);
    for (int k = 0; k < 3; k++) tr[k] = transVel[k] * loss;
    tr[1] = tr[1] + gravity;
    memcpy(ro, rotVel, 16);
    for (int k = 0; k < 3; k++) ro[k] = rotVel[k] * loss;
    for (int k = 0; k < 3; k++) pObj->position[k] = pObj->position[k] + tr[k];
    for (int k = 0; k < 3; k++) pObj->angle[k] = pObj->angle[k] + ro[k];
    for (int k = 0; k < 3; k++) gimk_angle_wrap(&pObj->angle[k]);
}

/* bounce: velocity scaled by loss * elasticity with y reflected (gravity too), spin reversed; 0 = no bounce
 * (elasticity 0: the object keeps falling) */
static int __DestPGRefrect(__data_dest_pg_st *pData, float (*pTrans)[4], float (*pRot)[4], float loss, float gravity)
{
    PS2_FRAME(0x40);
    float e = __DestPGReflect(pData->elasticType);
    if (e == 0.0f) return 0;
    float s = loss * e;
    for (int k = 0; k < 3; k++) (*pTrans)[k] = (*pTrans)[k] * s;
    float ge = gravity * e;
    (*pTrans)[1] = -((*pTrans)[1] + ge);
    float ns = -s;
    for (int k = 0; k < 3; k++) (*pRot)[k] = (*pRot)[k] * ns;
    (*pRot)[3] = 1.0f;
    return 1;
}

static void __DestPGEnd(GimkSt *pGimk)
{
    PS2_FRAME(0x30);
    StModel *m = gimk_pg_model(pGimk, gimk_pg_nn((__data_dest_pg_st *)GIMK_TD(pGimk)));
    m->use |= 7;
    m->mflag |= 0x80;
    gimk_kill(pGimk);
}

/* gravity curve over the destruction time r = now / destFrame: 0 r², 1 r, 2 1, 3 1 + r, 4 2(1 + r) */
static float __GetGravCof(int type, unsigned int nowFrame, unsigned int destFrame)
{
    switch (type) {
    case 0: {
        float r = ps2_div((float)nowFrame, (float)destFrame);
        return r * r;
    }
    case 1: return ps2_div((float)nowFrame, (float)destFrame);
    case 2: return 1.0f;
    case 3: return 1.0f + ps2_div((float)nowFrame, (float)destFrame);
    case 4: return (1.0f + ps2_div((float)nowFrame, (float)destFrame)) * 2.0f;
    }
    return 1.0f;
}

/* start delay: distance from the hit in units of radius / ((nobj - 2) / 8), times the trans coefficient */
static int __DestPGGetTransDelay(StModel *pMdl, float dist, int transCof)
{
    float n8 = ps2_div((float)(pMdl->nobj - 2), 8.0f);
    float seg = ps2_div(pMdl->mlist->sphere[3], n8);
    return (int)((unsigned int)transCof * (unsigned int)ps2_ftoi(ps2_div(dist, seg)));
}

/* ---- DestSfx: effects and sounds of the debris model (two sets in its typedata / wk, + 0x10 for the second) ---- */

static void __gimk_InitDestSfx(void) {}

/* the interval with intvlRand jitter: + or - rand % intvlRand */
static int gimk_jitter(int s, __data_dest_sfx_st *ds)
{
    if (!ds->intvlRand) return s;
    int j = 0;
    if (ds->intvlRand > 0) j = gimk_rand_range(ds->intvlRand);
    if (gimk_frac() <= 0.0f) j = -j;
    return s + j;
}

/* effect due: from delayFrame to delayFrame + sfxFrame every intvlFrame (+ jitter) frames, and at frame 1 */
static int __IsGenerateSfx(__data_dest_sfx_st *pData, __gimk_dest_sfx_st *pDestSfx, unsigned int progFrame)
{
    PS2_FRAME(0x40);
    if (!pData->sfxID) return 0;
    unsigned int end = pData->sfxFrame + pData->delayFrame;
    if (progFrame == 1 && pData->intvlFrame > 0) return 1;
    if (end < progFrame) return 0;
    if (!(pData->delayFrame < progFrame)) return 0;
    if (!((int)pDestSfx->lastGeneSndFrame < (int)end)) return 0;   /* quirk: the sound's frame, not the effect's */
    int s = progFrame - pDestSfx->lastGeneSfxFrame;
    s = gimk_jitter(s, pData);
    return pData->intvlFrame < s;
}

/* sound due: with the first effect, then every sndRateNum-th effect and intvlFrame (+ jitter) frames apart */
static int __IsGenerateSnd(__data_dest_sfx_st *pData, __gimk_dest_sfx_st *pDestSfx, unsigned int progFrame)
{
    PS2_FRAME(0x40);
    if (!pData->sndID) return 0;
    unsigned int end = pData->sfxFrame + pData->delayFrame;
    if (end < progFrame) return 0;
    if (!(pData->delayFrame < progFrame)) return 0;
    if (!((int)pDestSfx->lastGeneSndFrame < (int)end)) return 0;
    if (pDestSfx->nowGeneSfxNum == 1 && pDestSfx->lastGeneSfxFrame == progFrame) return 1;
    if (pData->sndRateNum >= 2 && ps2_mod32(pDestSfx->nowGeneSfxNum, pData->sndRateNum)) return 0;
    int s = progFrame - pDestSfx->lastGeneSndFrame;
    s = gimk_jitter(s, pData);
    return pData->intvlFrame < s;
}

static void __DestSfxProc(GimkSt *pGimk, StModel *pMdl, unsigned int progFrame, int bOft)
{
    PS2_FRAME(0xa0);
    __data_dest_sfx_st *ds = (__data_dest_sfx_st *)pMdl->array->typedata;
    __gimk_dest_sfx_st *st = (__gimk_dest_sfx_st *)pGimk->wk;
    if (bOft) {
        ds++;
        st++;
    }
    if (__IsGenerateSfx(ds, st, progFrame)) {
        PS2_STACK(float[4], pos, 0x90);
        __GetSfxGeneretaPos(pMdl, ds->generateType, pos);
        if (ds->isSpell)
            DF_SPELL2_AppearSpell((short)ds->sfxID, 0, 0, nullptr, nullptr, pos, ZeroVectorF, nullptr, nullptr, nullptr,
                                  0, 0);
        else {
            PS2_STACK(float[4], ang, 0x80);
            PS2_STACK(float[4], scl, 0x70);
            ang[0] = ang[1] = ang[2] = 0.0f;
            ang[3] = 1.0f;
            scl[0] = scl[1] = scl[2] = scl[3] = 1.0f;
            DF_MAGIC2_AppearMagic(ds->sfxID, pos, ang, scl, nullptr, nullptr, nullptr, 0);
        }
        st->nowGeneSfxNum++;
        st->lastGeneSfxFrame = progFrame;
    }
    if (__IsGenerateSnd(ds, st, progFrame)) {
        Voice_CallPoint(ds->sndID, 0x3c007f, (float (*)[4])pMdl->loc, (float)ds->sndDist);
        st->nowGeneSndNum++;
        st->lastGeneSndFrame = progFrame;
    }
}

/* where a debris effect appears: 3 random point in the model's cylinder (radius R, height tall), 2 a random
 * object, 1 the bounding-sphere centre, else the model location */
static void __GetSfxGeneretaPos(StModel *pMdl, int generateType, float *pos)
{
    PS2_FRAME(0x40);
    switch (generateType) {
    case 3: {
        float h = pMdl->tall * gimk_frac();
        float u = (float)(ps2_rand() & 0xffff) / ps2_u2f(0x477fff00);
        float r = pMdl->mlist->R * u;
        float a = GIMK_PI * gimk_frac();
        memcpy(pos, pMdl->mlist->sphere, 16);
        pos[0] = pos[0] + r * fsmath_SinVU0(a);
        pos[1] = pos[1] + h;
        pos[2] = pos[2] + r * fsmath_CosVU0(a);
        pos[3] = 1.0f;
        break;
    }
    case 2: {
        int i = pMdl->nobj > 0 ? gimk_rand_range(pMdl->nobj) : 0;
        memcpy(pos, (*pMdl->op[i].wrmatp)[3], 16);
        break;
    }
    case 1:
        memcpy(pos, pMdl->mlist->sphere, 16);
        pos[3] = 1.0f;
        break;
    default:
        memcpy(pos, pMdl->loc, 16);
        break;
    }
}
