/* src/event/evlight.cpp - stage light colour animation. Up to 32 animators (s_Lit, one per stage light) play
 * LitDataSt entries from the stage's light data: blend color1 -> color2 over `frame` frames (linear, sine, or a
 * hard switch at half time; lflag bit 0 = there and back), hold for `wait` frames, repeat `repeat` times
 * (0xff = forever), then chain to entry `next` (< 0x80) or stop. The result goes to the light's ambient row col[3]. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

static void __EvLight_Exec(_lit_tag *lhp)
{
    LitDataSt *ld = s_pLitData + lhp->litdata_id;
    if (lhp->state == 1) return;
    PS2_FRAME(0x60);
    if (lhp->state == 2) {
        if (ld->frame < lhp->nowframe) {
            lhp->nowframe = 0;
            lhp->state = 3;
        } else {
            int now = lhp->nowframe, frame = ld->frame;
            if (ld->intp == 3) {   /* switch to color2 at half time */
                PS2_STACK(float[4], c, 0x30);
                int second = ps2_div32(now * 2, frame);
                for (int i = 0; i < 4; i++) {
                    c[i] = (float)(second ? ld->color2[i] : ld->color1[i]) / 255.0f;
                    lhp->mlp->col[3][i] = c[i];
                }
            } else if (ld->intp == 2) {   /* sine ease (lflag 1: up and back down) */
                float r = ps2_div((float)now, (float)frame);
                float s = fsmath_SinVU0((ld->lflag & 1 ? 3.1415927f : 1.5707964f) * r);
                float w1 = 1.0f - s;
                PS2_STACK(float[4], c, 0x40);
                for (int i = 0; i < 4; i++) {
                    c[i] = ((float)ld->color1[i] * w1 + (float)ld->color2[i] * s) / 255.0f;
                    lhp->mlp->col[3][i] = c[i];
                }
            } else if (ld->intp == 1) {   /* linear (lflag 1: up and back down) */
                float t;
                if (ld->lflag & 1) {
                    t = ps2_div((float)now, ps2_div((float)frame, 2.0f));
                    if (!(t <= 1.0f)) t = 2.0f - t;
                } else {
                    t = ps2_div((float)now, (float)frame);
                }
                float w1 = 1.0f - t;
                PS2_STACK(float[4], c, 0x50);
                for (int i = 0; i < 4; i++) {
                    c[i] = ((float)ld->color1[i] * w1 + (float)ld->color2[i] * t) / 255.0f;
                    lhp->mlp->col[3][i] = c[i];
                }
            }
            lhp->nowframe++;
        }
    }
    if (lhp->state == 3) {
        if (lhp->nowframe < ld->wait) {
            lhp->nowframe++;
        } else if (lhp->nowrepeat < ld->repeat) {
            lhp->nowframe = 0;
            if (ld->repeat != 0xff) lhp->nowrepeat++;
            lhp->state = 2;
        } else if (ld->next < 0x80) {
            lhp->litdata_id = (unsigned char)ld->next;
            lhp->state = 2;
            lhp->nowframe = 0;
            lhp->nowrepeat = 0;
        } else {
            lhp->state = 1;
            lhp->exec = 0;
        }
    }
}

void DF_LIT_Set(int lit_id, int litdata_id)
{
    _lit_tag *p = &s_Lit[lit_id];
    p->state = 2;
    p->lit_id = (unsigned char)lit_id;
    p->litdata_id = (unsigned char)litdata_id;
    p->nowrepeat = 0;
    p->nowframe = 0;
    p->mlp = StgInfo.light + lit_id;
    p->exec = GFN(__EvLight_Exec);
}

void DF_LIT_Proc(void)
{
    PS2_FRAME(0x30);
    for (int i = 0; i < 32; i++) {
        _lit_tag *p = &s_Lit[i];
        if (p->exec) gcall(p->exec, p);
    }
}

void DF_LIT_Init(void *data)
{
    s_pLitData = (LitDataSt *)data;
    PS2_FRAME(0x10);
    memset(s_Lit, 0, 0x200);
    for (int i = 0; i < 32; i++) {
        unsigned char id = Array->liteff[i];
        if (!id) continue;
        _lit_tag *p = &s_Lit[i];
        p->state = 2;
        p->lit_id = (unsigned char)i;
        p->litdata_id = id;
        p->nowrepeat = 0;
        p->nowframe = 0;
        p->mlp = StgInfo.light + i;
        p->exec = GFN(__EvLight_Exec);
    }
}
