/* src/event/event_term.cpp - mission-term tests of the stage event manager: each EventTerm_* checks one condition of
 * a mission command (__cmd_mission_st) and clears *pRunFlag when it does not hold (never sets it).
 * Native: EventTerm_EnemyRemainNum, EventTerm_TouchDown, EventTerm_ModelApRate (garage3). The other 18 EventTerm_*
 * functions stay interpreted. */
#include "acnb/port.h"
#include "acnb/ps2fp.h"

/* Info_GetData(type, id, 0, 0).i with the sret slot at the original's sp + slot */
static int eterm_info_get(int type, int id, unsigned int slot)
{
    PS2_STACK(union_9a8d3bef, r, slot);
    gcall_sret(GFN(Info_GetData), &r, type, id, 0, 0);
    return r.i;
}

/* surviving enemies (type 0 = info (3, 6), 1 = (3, 7), 2 = their sum; any other type compares against INT_MAX)
 * against survivor_num.num; authenticity 0: clear when num < cnt, 1: cnt < num, 2: num != cnt, 3: num == cnt */
void EventTerm_EnemyRemainNum(__cmd_mission_st *pCmdMission, int *pRunFlag)
{
    PS2_FRAME(0x50);
    int cnt = 0x7fffffff;
    switch (pCmdMission->survivor_num.type) {
    case 0: cnt = eterm_info_get(3, 6, 0x4c); break;
    case 1: cnt = eterm_info_get(3, 7, 0x4c); break;
    case 2:
        cnt = eterm_info_get(3, 6, 0x4c);
        cnt += eterm_info_get(3, 7, 0x4c);
        break;
    default: break;
    }
    int num = pCmdMission->survivor_num.num;   /* lhu: zero-extended, signed compares */
    switch (pCmdMission->survivor_num.flag.authenticity) {
    case 0: if (num < cnt) *pRunFlag = 0; break;
    case 1: if (cnt < num) *pRunFlag = 0; break;
    case 2: if (num != cnt) *pRunFlag = 0; break;
    case 3: if (num == cnt) *pRunFlag = 0; break;
    }
}

/* Info_GetData(type, id, arg1, 0).i with the sret slot at the original's sp + slot */
static int eterm_info_get3(int type, int id, int arg1, unsigned int slot)
{
    PS2_STACK(union_9a8d3bef, r, slot);
    gcall_sret(GFN(Info_GetData), &r, type, id, arg1, 0);
    return r.i;
}

/* model terms (__cmd_model_st). touchdown: event model type 4 (AC) has touched down = not airborne (actstat != 1)
 * or, for legs type 4, airborne over water within 0.001 above the surface and not deeper than
 * its height; authenticity 2: clear when not touched down, 3: when touched down. Any other model type clears.
 * Quirk: type 3 (enemy) fetches the model and discards it, so an enemy never touches down. */
void EventTerm_TouchDown(__cmd_model_st *pCmdModel, int *pRunFlag)
{
    PS2_FRAME(0x40);
    int down = 0;
    __evmng_event_id_st *ev = EVMNG_GetEventID(pCmdModel->touchdown.event_id);
    switch (ev->type) {
    case 4: {
        AcPlayerSt *acp = &g_ac[ev->id];
        if (acp->param.legsp->type == 4) {
            if (acp->actstat != 1) {
                down = 1;
            } else {
                float y = acp->loc[1];
                int on = 0;
                if (acp->water_high < ps2_u2f(0x3a83126f) + y)
                    if (!(acp->water_high <= y - acp->tall)) on = 1;
                if (on) down = 1;
            }
        } else if (acp->actstat != 1) {
            down = 1;
        }
        break;
    }
    case 3:
        STAGE_GetModelPointer(3, ev->id);
        break;
    default:
        *pRunFlag = 0;
        return;
    }
    switch (pCmdModel->touchdown.flag.authenticity) {
    case 3: if (down == 1) *pRunFlag = 0; break;
    case 2: if (down == 0) *pRunFlag = 0; break;
    }
}

/* ap_ratio: AP percentage (0..100) of the event model against ratio; authenticity 0: clear when rate > ratio,
 * 1: rate < ratio, 2: rate != ratio, 3: rate == ratio; negative AP clears. Types 1 / 2 / 8 read the StModel, 4 / 3
 * the AC / enemy info tables (max first). Quirk: any other type uses the stale stack words (cur sp+0x58, max sp+0x5c),
 * and STAGE_GetModelPointer is called for every type. */
void EventTerm_ModelApRate(__cmd_model_st *pCmdModel, int *pRunFlag)
{
    PS2_FRAME(0x60);
    PS2_STACK(int, cur, 0x58);
    PS2_STACK(int, mx, 0x5c);
    float ratio = (float)pCmdModel->ap_ratio.ratio;
    __evmng_event_id_st *ev = EVMNG_GetEventID(pCmdModel->ap_ratio.event_id);
    StModel *m = STAGE_GetModelPointer(ev->type, ev->id);
    switch (ev->type) {
    case 8: case 1: case 2:
        cur = m->nowhp;
        mx = m->array->hp;
        break;
    case 4:
        mx = eterm_info_get3(1, 0, ev->id, 0x5c);
        cur = eterm_info_get3(1, 1, ev->id, 0x58);
        break;
    case 3:
        mx = eterm_info_get3(2, 0, ev->id, 0x5c);
        cur = eterm_info_get3(2, 1, ev->id, 0x58);
        break;
    default:
        break;
    }
    if (cur < 0) {
        *pRunFlag = 0;
        return;
    }
    float rate = ps2_div(100.0f * (float)cur, (float)mx);
    if (rate < 0.0f) rate = 0.0f;
    if (!(rate <= 100.0f)) rate = 100.0f;
    switch (pCmdModel->ap_ratio.flag.authenticity) {
    case 0: if (!(rate <= ratio)) *pRunFlag = 0; break;
    case 1: if (rate < ratio) *pRunFlag = 0; break;
    case 2: if (rate != ratio) *pRunFlag = 0; break;
    case 3: if (rate == ratio) *pRunFlag = 0; break;
    }
}
