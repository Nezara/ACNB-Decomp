/* src/menu/mhelp.cpp - the "help" overlay of the garage / assembly screens.
 * A pool of 16 help LISTS (one per window, DF_MHELP_NewList(type)) each owning a doubly linked chain of help PARAMS
 * (128-entry pool: category id + string id + a reference mobj whose help position the cursor snaps to). While
 * MenuMng.f_help is set (DF_MHELP_Mode(1)), DF_MHELP_Play runs __CsrPlay (cursor fade in / out + blink alpha), polls
 * the help dialog: state 4 closes help (Mode(0)), state 2 runs __PadPlay (D-pad moves the cursor between params /
 * lists, click voice when the selection changed) and re-sets the dialog text. DF_MHELP_Draw draws the cursor sprite,
 * DF_MHELP_DrawBack the dark full-screen veil. The pools live in menu work memory (DF_MHELP_Init); DF_MHELP_Reset runs
 * every __MenuStart.
 * Original quirks kept: Reset clears only 16 of the 128 params (ClearMemory counts words); Select / Mode / Play read
 * through a possibly NULL cur_prmp (PS2 address 0 -> PS2_NULL_MEM), Select walks the chain without bounds and leaves
 * csr.l_rect stale; __PadPlay's list switch falls back to the list's top param even when it is disabled, its searches
 * stop after 99 steps. DF_MBGUIDE_Change is the retail empty stub (the stale extra arguments are not mirrored). */
#include "acnb/sdk.h"

/* reads through a pointer the original does not NULL-check: PS2 address 0 */
static inline const struct _mhelp_param_st *mh_prm(const struct _mhelp_param_st *p)
{
    return p ? p : (const struct _mhelp_param_st *)PS2_NULL_MEM();
}
static inline const struct _mhelp_list_st *mh_lst(const struct _mhelp_list_st *l)
{
    return l ? l : (const struct _mhelp_list_st *)PS2_NULL_MEM();
}
/* p = p->next, idx times (no bounds / NULL check, MWCC unrolled it by 8) */
static inline struct _mhelp_param_st *mh_walk(struct _mhelp_param_st *p, int idx)
{
    for (int i = 0; i < idx; i++) p = mh_prm(p)->next;
    return p;
}

void DF_MHELP_SetParamEnable(struct _mhelp_param_st *ioParam, int inFlag)
{
    if (ioParam) ioParam->f_enable = (char)(inFlag & 1);
}

struct _mhelp_param_st *DF_MHELP_GetParam(struct _mhelp_list_st *inList, int inIdx)
{
    if (inIdx < 0 || inIdx >= inList->count) return 0;
    return mh_walk(inList->top, inIdx);
}

void DF_MHELP_Select(struct _mhelp_list_st *inList, int inIdx)
{
    HelpMng.cur_listp = inList;
    if (inIdx == -1) inIdx = inList->cur_idx;
    HelpMng.cur_prmp = mh_walk(inList->top, inIdx);
    inList->cur_idx = (char)inIdx;
    const struct _mhelp_param_st *p = mh_prm(HelpMng.cur_prmp);   /* no NULL check (sic) */
    HelpMng.csr.rect.x = p->x - 0x14;
    HelpMng.csr.rect.y = p->y - 2;   /* csr.l_rect left stale (sic) */
}

struct _mhelp_param_st *DF_MHELP_GetLastParam(struct _mhelp_list_st *ioList)
{
    struct _mhelp_param_st *p = ioList->top;
    if (!p) return 0;
    while (p->next) p = p->next;
    return p;
}

void DF_MHELP_DeleteParamFromList(struct _mhelp_list_st *ioList, struct _mhelp_param_st *ioParam)
{
    PS2_FRAME(0x20);
    struct _mhelp_param_st *prev = ioParam->prev;
    struct _mhelp_param_st *next = ioParam->next;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    if (ioList->top == ioParam) ioList->top = next;
    DF_MFUNC_ClearMemory(ioParam, 8);
    ioList->count = (char)(ioList->count - 1);
    if (ioList->count <= 0) {
        ioList->count = 0;
        ioList->top = 0;
    }
}

void DF_MHELP_AddParamToList(struct _mhelp_list_st *ioList, struct _mhelp_param_st *inParam)
{
    struct _mhelp_param_st *t = ioList->top;
    if (!t) {
        ioList->top = inParam;   /* inParam->prev not written (sic) */
    } else {
        while (t->next) t = t->next;
        t->next = inParam;
        inParam->prev = t;
    }
    ioList->count = (char)(ioList->count + 1);
}

struct _mhelp_param_st *DF_MHELP_NewParam(int inCtgrID, int inStrID, struct stMobj *inRefObj)
{
    PS2_FRAME(0x60);
    struct _mhelp_param_st *p = HelpMng.top_prmp;
    int i;
    for (i = 0; i < 0x80; i++, p++) {
        if (!p->f_use) {
            DF_MFUNC_ClearMemory(p, 8);
            break;
        }
    }
    if (i >= 0x80) return 0;
    p->f_use = 1;
    p->f_enable = 1;
    p->ctgrID = inCtgrID;
    p->strID = inStrID;
    p->ref_obj = inRefObj;
    if (p->ref_obj) {
        p->ref_obj->f_help = 1;   /* never cleared again (sic) */
        int (*pos)[4] = DF_MOBJ_GetHelpPos(p->ref_obj);
        p->x = (short)(*pos)[0];
        p->y = (short)(*pos)[1];
    }
    return p;
}

void DF_MHELP_DeleteList(struct _mhelp_list_st *ioList)
{
    for (struct _mhelp_param_st *p = ioList->top; p; p = p->next) p->f_use = 0;
    struct _mhelp_list_st *prev = ioList->prev;
    struct _mhelp_list_st *next = ioList->next;
    if (prev) prev->next = next;
    if (next) next->prev = prev;
    DF_MFUNC_ClearMemory(ioList, 4);   /* tail call; HelpMng.cur_listp / top_listp not updated (sic) */
}

struct _mhelp_list_st *DF_MHELP_NewList(int inType)
{
    PS2_FRAME(0x40);
    struct _mhelp_list_st *l = HelpMng.top_listp;
    int i;
    for (i = 0; i < 0x10; i++, l++) {
        if (!l->f_use) {
            DF_MFUNC_ClearMemory(l, 4);
            l->f_use = 1;
            break;
        }
    }
    if (i >= 0x10) return 0;
    if (l != HelpMng.top_listp) {
        struct _mhelp_list_st *t = HelpMng.top_listp;
        while (t->next) t = t->next;
        t->next = l;
        l->prev = t;
    } else {
        HelpMng.cur_listp = l;
    }
    l->type = (char)inType;
    return l;
}

void DF_MHELP_DrawBack(void)
{
    PS2_FRAME(0x30);
    PS2_STACK(struct stMenuRect, rect, 0x10);     /* OLMENU 0x1ede270, lq / sq */
    PS2_STACK(struct struct_76b810ec, col, 0x2c); /* main 0x27b850 = 0x40000000 */
    rect.x = 0;
    rect.y = 0;
    rect.w = 0x280;
    rect.h = 0x1c0;
    col.r = 0;
    col.g = 0;
    col.b = 0;
    col.a = 0x40;
    if (MenuMng.f_help) {
        DF_MDRAW_KickReg(0x47, 0x3000f);   /* TEST_1 */
        DF_MDRAW_KickReg(0x42, 0x44);      /* ALPHA_1 */
        DF_MDRAW_DrawColSprite(&rect, 1, &col, 0);
    }
}

void DF_MHELP_Draw(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(int[4], uv, 0x10);   /* OLMENU 0x1ede260, lq / sq */
    uv[0] = 0xb8;
    uv[1] = 0x20;
    uv[2] = 0x98;
    uv[3] = 0;
    if (!MenuMng.f_help || !HelpMng.cur_prmp) return;
    DF_MDRAW_KickReg(0x47, 0x3000f);
    DF_MDRAW_KickReg(0x42, 0x48);
    DF_MDRAW_DrawTexSpriteI(0x3ec, &HelpMng.csr.l_rect, uv, &HelpMng.csr.l_col, 0);
}

void DF_MHELP_Play(void)
{
    PS2_FRAME(0x20);
    if (!MenuMng.f_help) return;
    __CsrPlay();
    if (DF_MDIALOG_Sync() == 4) {
        DF_MHELP_Mode(0);
        return;
    }
    if (DF_MDIALOG_Sync() != 2) return;
    struct _mhelp_param_st *old = HelpMng.cur_prmp;
    HelpMng.cur_prmp = __PadPlay();
    if (old == HelpMng.cur_prmp) return;
    const struct _mhelp_param_st *p = mh_prm(HelpMng.cur_prmp);
    int pos = (p->y < 0xe1) ? 2 : 1;
    DF_MDIALOG_Set(DF_MSTR_GetInfo(MenuMng.language, p->ctgrID, p->strID), pos);
    p = mh_prm(HelpMng.cur_prmp);
    HelpMng.csr.rect.y = p->y - 2;
    HelpMng.csr.rect.x = p->x - 0x14;
    HelpMng.csr.l_rect.x = HelpMng.csr.rect.x - 4;
    HelpMng.csr.l_rect.y = HelpMng.csr.rect.y - 4;
}

/* __PadPlay's list switch tail: the list's remembered param, else its top (even a disabled top, sic) */
static struct _mhelp_param_st *mh_pick(void)
{
    struct _mhelp_list_st *l = HelpMng.cur_listp;
    int i = l->cur_idx;
    struct _mhelp_param_st *s = (i < 0 || i >= l->count) ? 0 : mh_walk(l->top, i);
    if (!s) return l->top;
    if (!s->f_enable) return l->top;
    return s;
}

struct _mhelp_param_st *__PadPlay(void)
{
    PS2_FRAME(0x20);
    int n = 0;          /* step counter (a0) */
    int found = 0;      /* v1 */
    int dir = 0;        /* a1 */
    struct _mhelp_list_st *l = HelpMng.cur_listp;
    struct _mhelp_param_st *cur = HelpMng.cur_prmp;   /* v0 */
    struct _mhelp_param_st *s = cur;                  /* s0, the result */
    const struct struct_81b11733 &rep = MPad[MPadUser].repeat;

    if (mh_lst(l)->type == 0) {
        if (rep.dw) dir = 4;
        else if (rep.up) dir = 3;
        else if (rep.rt) dir = 2;
        else if (rep.lt) dir = 1;
    } else {
        if (rep.dw) dir = 2;
        else if (rep.up) dir = 1;
        else if (rep.rt) dir = 4;
        else if (rep.lt) dir = 3;
    }

    if (dir == 3) {
        /* previous param */
        if (!mh_prm(cur)->prev) {
            while (mh_prm(s)->next) s = s->next;   /* last param */
            l->cur_idx = (char)(l->count - 1);
            if (mh_prm(s)->f_enable == 1) goto end;
        }
        for (;;) {
            struct _mhelp_param_st *pv = mh_prm(s)->prev;
            if (!pv) break;
            if (n++ >= 99) break;
            HelpMng.cur_listp->cur_idx = (char)(HelpMng.cur_listp->cur_idx - 1);
            s = pv;
            if (pv->f_enable == 1) break;
            if (!pv->prev) {
                while (s->next) s = s->next;   /* wrap to the last param */
                HelpMng.cur_listp->cur_idx = (char)(HelpMng.cur_listp->count - 1);
                if (s->f_enable == 1) break;
            }
        }
    } else if (dir == 4) {
        /* next param */
        if (!mh_prm(cur)->next) {
            s = l->top;
            l->cur_idx = 0;
            if (mh_prm(s)->f_enable == 1) goto end;
        }
        for (;;) {
            struct _mhelp_param_st *nx = mh_prm(s)->next;
            if (!nx) break;
            if (n++ >= 99) break;
            HelpMng.cur_listp->cur_idx = (char)(HelpMng.cur_listp->cur_idx + 1);
            s = nx;
            if (nx->f_enable == 1) break;
            if (!nx->next) {
                s = HelpMng.cur_listp->top;   /* wrap to the top */
                HelpMng.cur_listp->cur_idx = 0;
                if (mh_prm(s)->f_enable == 1) break;
            }
        }
    } else if (dir == 1) {
        /* previous list */
        if (!l->prev) {
            while (l->next) l = l->next;   /* last list */
            if (l->count > 0) found = 1;
        }
        if (!found) {
            for (;;) {
                struct _mhelp_list_st *pv = l->prev;
                if (!pv) break;
                if (n++ >= 99) break;
                l = pv;
                if (pv->count > 0) {
                    found = 1;
                    break;
                }
            }
        }
        if (found == 1) {
            HelpMng.cur_listp = l;
            s = mh_pick();
        }
    } else if (dir == 2) {
        /* next list */
        if (!l->next) {
            l = HelpMng.top_listp;
            if (l->count > 0) found = 1;
        }
        if (!found) {
            for (;;) {
                struct _mhelp_list_st *nx = l->next;
                if (!nx) break;
                if (n++ >= 99) break;
                l = nx;
                if (nx->count > 0) {
                    found = 1;
                    break;
                }
            }
        }
        if (found == 1) {
            HelpMng.cur_listp = l;
            s = mh_pick();
        }
    }
end:
    if (cur != s) Voice_Call(0x3e9, 0x7f);
    return s;
}

void __CsrPlay(void)
{
    PS2_FRAME(0x20);
    if (MPad[MPadUser].Rrt == 1 && HelpMng.csr.stat == 0) {
        if (!DF_MBGUIDE_Sync(0)) return;
        HelpMng.csr.stat = 2;
        PS2_STACK(unsigned long long, arg9, 0);   /* 9th argument 0x63 of the key list */
        arg9 = 0x63;
        (void)arg9;
        DF_MBGUIDE_Change();   /* (pre_kind[0..7], 0x63): retail empty stub */
    }
    int b = DF_MFUNC_GetBlinkAlpha();
    if (HelpMng.csr.stat == 2) {
        HelpMng.csr.col.a = (unsigned char)(HelpMng.csr.col.a - 0x20);
        if (HelpMng.csr.col.a == 0) {
            HelpMng.csr.col.a = 0;
            HelpMng.csr.stat = 3;
        }
    } else if (HelpMng.csr.stat == 1) {
        HelpMng.csr.col.a = (unsigned char)(HelpMng.csr.col.a + 0x20);
        if (HelpMng.csr.col.a >= 0x80) {
            HelpMng.csr.stat = 0;
            HelpMng.csr.col.a = 0x80;
        }
    }
    HelpMng.csr.l_col.a = (unsigned char)((b * (int)HelpMng.csr.col.a) / 128);
}

void DF_MHELP_Mode(int inMode)
{
    PS2_FRAME(0x40);
    int ctgr = 0, str = 0;
    MenuMng.f_help = (char)inMode;
    if (inMode != 1) return;
    if (HelpMng.cur_listp && !HelpMng.cur_listp->f_use) HelpMng.cur_listp = 0;
    struct _mhelp_param_st *p = HelpMng.cur_prmp;
    if (p && (!p->f_use || !p->f_enable)) HelpMng.cur_prmp = 0;
    DF_MBGUIDE_GetKind(HelpMng.pre_kind);
    DF_MBGUIDE_Change();   /* (0, 2, 0, 0, 0x63, stale...): retail empty stub */
    if (!HelpMng.cur_prmp) {
        if (!HelpMng.cur_listp) HelpMng.cur_listp = HelpMng.top_listp;
        else if (HelpMng.cur_listp->count == 0) HelpMng.cur_listp = HelpMng.top_listp;
        struct _mhelp_list_st *l = HelpMng.cur_listp;
        int i = l->cur_idx;
        struct _mhelp_param_st *q = (i < 0 || i >= l->count) ? 0 : mh_walk(l->top, i);
        if (!q) {
            q = l->top;
            l->cur_idx = 0;
        } else if (!q->f_enable) {
            q = l->top;
            l->cur_idx = 0;
        }
        HelpMng.cur_prmp = q;
    }
    p = HelpMng.cur_prmp;
    if (p) {
        ctgr = p->ctgrID;
        str = p->strID;
    }
    int pos = (mh_prm(p)->y < 0xe1) ? 2 : 1;   /* read even when p is NULL (sic) */
    DF_MDIALOG_Open(3, DF_MSTR_GetInfo(MenuMng.language, ctgr, str), 0, 0, 0, pos, 0, 0);
    const struct _mhelp_param_st *c = mh_prm(HelpMng.cur_prmp);
    HelpMng.csr.rect.y = c->y - 2;
    HelpMng.csr.rect.x = c->x - 0x14;
    HelpMng.csr.stat = 1;
    HelpMng.csr.col.a = 0;
    HelpMng.csr.l_rect.x = HelpMng.csr.rect.x - 4;
    HelpMng.csr.l_rect.y = HelpMng.csr.rect.y - 4;
}

void DF_MHELP_Reset(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(struct struct_76b810ec, col, 0x1c);     /* main 0x27b848 = 0x80808080 */
    PS2_STACK(struct struct_76b810ec, l_col, 0x18);   /* main 0x27b84c = 0x80808080 */
    col.r = col.g = col.b = col.a = 0x80;
    l_col.r = l_col.g = l_col.b = l_col.a = 0x80;
    DF_MFUNC_ClearMemory(HelpMng.top_listp, 0x40);   /* all 16 lists */
    DF_MFUNC_ClearMemory(HelpMng.top_prmp, 0x80);    /* 0x80 words = only 16 of the 128 params (sic) */
    DF_MFUNC_ClearMemory(&HelpMng, 0xd);             /* csr */
    HelpMng.csr.rect.w = 0x10;
    HelpMng.csr.rect.h = 0x10;
    HelpMng.csr.l_rect.w = 0x18;
    HelpMng.csr.l_rect.h = 0x18;
    HelpMng.csr.col = col;
    HelpMng.csr.l_col = l_col;
    HelpMng.csr.stat = 3;
    HelpMng.cur_listp = 0;
    HelpMng.cur_prmp = 0;
}

unsigned char *DF_MHELP_Init(unsigned char *ioBuff)
{
    PS2_FRAME(0x10);
    unsigned char *v = (unsigned char *)DF_MFUNC_Aligned(ioBuff, 0x10);
    HelpMng.top_listp = (struct _mhelp_list_st *)v;
    HelpMng.top_prmp = (struct _mhelp_param_st *)(v + 0x100);
    return v + 0x1100;
}
