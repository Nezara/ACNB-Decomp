/* src/stage/item_map.cpp - map items: weapons / parts dropped by ACs (purged weapons, removed extensions), up to 40.
 * DF_ITM_New takes one or two models and a part record (filled by the caller through DF_ITM_GetDataAddr); each frame
 * the models fall under gravity until a vertical het ray finds the floor, optionally with a map label;
 * DF_ITM_PickUp scores the items in front of an AC (distance, pitch and yaw offsets) and returns the best one. */
#include <string.h>
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

static const float ITM_PI_4 = 0.785398185f;   /* 0x3f490fdb */

/* floor below the first model: a ray from 1.5 below to 10000 above; the height only rises (reset: start at the
 * ray origin) */
static int __hitchk(struct_af8a9da8 *itm, int reset)
{
    PS2_FRAME(0x50);
    StModel *m = 0;
    for (int k = 0; k < 2; k++) {
        m = itm->mp[k];
        if (m) break;
    }
    if (!m) return 0;
    float p0[4] __attribute__((aligned(16))), p1[4] __attribute__((aligned(16))), hit[4] __attribute__((aligned(16)));
    memcpy(p0, m->loc, 16);
    p0[1] = p0[1] - 1.5f;
    memcpy(p1, m->loc, 16);
    p1[1] = p1[1] + 10000.0f;
    if (reset) itm->fht = p0[1];
    if (HetProcPoint2Point(p0, p1, hit, 0) && !(hit[1] < itm->fht)) {
        itm->fht = hit[1];
        return 1;
    }
    return 0;
}

/* 1.0 = out of reach (30 units, 45 degrees off in pitch or yaw); else 0.25 * distance + 0.375 * |pitch| +
 * 0.375 * |yaw|, each part normalised to 0..1 */
static float __calc_pickupvl(float *pos, float *loc, float *ang)
{
    PS2_FRAME(0x40);
    float dx = pos[0] - loc[0], dy = pos[1] - loc[1], dz = pos[2] - loc[2];
    float dist = ps2_sqrt(dz * dz + (dx * dx + dy * dy));
    if (!(dist <= 30.0f)) return 1.0f;
    float ax = ps2_atan2f(-dy, ps2_sqrt(dx * dx + dz * dz)) - ang[0];
    if (!(ax <= ITM_PI_4) || ax < -ITM_PI_4) return 1.0f;
    float ay = ps2_atan2f(dx, dz) - ang[1];
    if (!(ay <= ITM_PI_4) || ay < -ITM_PI_4) return 1.0f;
    float fd = ps2_div(dist, 30.0f);
    if (!(fd <= 1.0f)) fd = 1.0f;
    if (ax < 0.0f) ax = -ax;
    ax = ps2_div(ax, ITM_PI_4);
    if (!(ax <= 1.0f)) ax = 1.0f;
    if (ay < 0.0f) ay = -ay;
    ay = ps2_div(ay, ITM_PI_4);
    if (!(ay <= 1.0f)) ay = 1.0f;
    float acc = 0.25f * fd + 0.375f * ax;
    return acc + 0.375f * ay;
}

/* place the model (at loc / its own matrix, facing ang / its matrix's z axis), mark it as an item, idle anim */
static void __init_model(StModel *mp, float *loc, float *ang)
{
    PS2_FRAME(0x20);
    if (!mp) return;
    memcpy(mp->loc, loc ? loc : mp->matrix2[3], 16);
    if (ang) memcpy(mp->ang, ang, 16);
    else vec2ang(mp->ang, mp->matrix2[2]);
    mp->use |= 0x80;
    for (int k = 0; k < mp->nobj; k++) {
        mp->op[k].group = 1;
        mp->op[k].todid = 0xff;
    }
    Anim_ChangeAnim(mp, 0, 0x3c, 0);
}

/* fall (y grows downwards) until the floor, hide while weapon pickup is enabled, decay the X tilt, rebuild the
 * matrices; once both have landed re-probe the floor: still on it -> settled, else fall again */
static void __exec(struct_af8a9da8 *itm)
{
    PS2_FRAME(0xf0);
    float mat[4][4] __attribute__((aligned(16)));
    int tilt = (unsigned char)itm->work[0] == 7;   /* (an extra quarter turn about X) */
    for (int k = 0; k < 2; k++) {
        StModel *m = itm->mp[k];
        if (!m) {
            itm->actflag |= (unsigned char)(1 << k);
            continue;
        }
        int bit = 1 << k;
        float vy = itm->vel[1], fht = itm->fht;
        if (!(itm->actflag & bit)) {
            m->loc[1] = m->loc[1] + vy;
            if (!(m->loc[1] <= fht)) {
                m->loc[1] = fht;
                itm->actflag |= (unsigned char)bit;
            }
        }
        if (itm->actflag & bit) {
            unsigned char on = MyI.pikwep_en == 0;
            for (int j = 0; j < m->nobj; j++) m->op[j].onoff = on;
        }
        m->ang[0] = m->ang[0] * 0.5f;
        m = itm->mp[k];
        Anim_Update(m);
        sceVu0RotMatrixX(mat, IdentityMtx, tilt ? ps2_u2f(0x3fc90fdb) + m->ang[0] : m->ang[0]);
        sceVu0RotMatrixY(mat, mat, m->ang[1]);
        sceVu0TransMatrix(m->matrix2, mat, m->loc);
        sceVu0CopyMatrix(*m->op[0].wrmatp, m->matrix2);
        if (m->nobj >= 2) Anim_UpdateChildObjMatrix(&m->op[1], m->matrix2);
    }
    if ((itm->actflag & 3) != 3) {
        itm->vel[1] = itm->vel[1] + ps2_u2f(0x3d2c0831);   /* 0.042f */
        return;
    }
    if (!__hitchk(itm, 0)) return;
    if (!itm->mp[0] && !itm->mp[1]) return;
    float y = 0.0f;
    if (itm->mp[0]) y = itm->mp[0]->loc[1];
    else if (itm->mp[1]) y = itm->mp[1]->loc[1];
    float dy = itm->fht - y;
    if (dy < ps2_u2f(0xbc23d70a) || !(dy < ps2_u2f(0x3d2c0831))) itm->actflag = 0;
    else itm->vel[0] = 0.0f;   /* (vel[0], not the fall speed vel[1]: the speed is never reset) */
}

/* best item in front of loc / ang (score below 1.0), returns its id or 0 (mp[0] unchecked) */
int DF_ITM_PickUp(float *loc, float *ang)
{
    PS2_FRAME(0x80);
    float best = 1.0f;
    struct_af8a9da8 *res = 0;
    for (int i = 0; i < 40; i++) {
        if (!ItemStat[i]) continue;
        float v = __calc_pickupvl(ItemInfo[i].mp[0]->loc, loc, ang);
        if (!(best <= v)) {
            best = v;
            res = &ItemInfo[i];
        }
    }
    if (res && best < 1.0f) return res->id;
    return 0;
}

void *DF_ITM_GetDataAddr(int itmid)
{
    if (!(itmid > 0 && itmid < 41)) return 0;
    return ItemInfo[itmid - 1].work;
}

void *DF_ITM_GetModel(int itmid, int index)
{
    if (!(itmid > 0 && itmid < 41)) return 0;
    if (!(index >= 0 && index < 2)) return 0;
    return ItemInfo[itmid - 1].mp[index];
}

/* (the record itself is not cleared) */
int DF_ITM_Delete(int itmid)
{
    PS2_FRAME(0x20);
    if (!(itmid > 0 && itmid < 41)) return -1;
    ItemStat[itmid - 1] = 0;
    struct_af8a9da8 *it = &ItemInfo[itmid - 1];
    if (it->hstr) DF_ACSTAT_DelMapString(it->hstr);
    if (it->mp[0]) it->mp[0]->use &= 0xff7f;
    if (it->mp[1]) it->mp[1]->use &= 0xff7f;
    return 0;
}

int DF_ITM_SetString(int itmid, int strid)
{
    PS2_FRAME(0x20);
    if (!MyI.pikwep_en) return -1;
    if (!(itmid > 0 && itmid < 41)) return -1;
    struct_af8a9da8 *it = &ItemInfo[itmid - 1];
    it->hstr = (char)DF_ACSTAT_AddMapString(it->mp[0], 0.0f, 0.0f, 0.0f, 100000.0f, strid);
    return 0;
}

/* the slot search starts at ItemNext (which only ever counts up); fht is overwritten by the floor probe */
int DF_ITM_New(float *loc, float *ang, StModel *mp1, StModel *mp2, float fht, void *argp)
{
    PS2_FRAME(0x90);
    if (!mp1) return -1;
    int s = ItemNext;
    int n = 0;
    for (; n < 40; n++, s++) {
        if (!(s < 40)) s %= 40;
        if (!ItemStat[s]) break;
    }
    if (n == 40) s = -1;
    else ItemNext++;
    if (s < 0) return -1;
    ItemStat[s] = 1;
    struct_af8a9da8 *it = &ItemInfo[s];
    memset(it, 0, 0x60);
    it->id = (unsigned char)(s + 1);
    it->mp[0] = mp1;
    it->mp[1] = mp2;
    it->argp = argp;
    it->fht = fht;
    it->vel[1] = ps2_u2f(0xbe570a3d);   /* -0.21f */
    __init_model(mp1, loc, ang);
    if (mp2) __init_model(mp2, loc, ang);
    __hitchk(it, 1);
    return it->id;
}

void DF_ITM_Exec(void)
{
    PS2_FRAME(0x40);
    for (int i = 0; i < 40; i++)
        if (ItemStat[i]) __exec(&ItemInfo[i]);
}

void DF_ITM_Init(void)
{
    PS2_FRAME(0x10);
    memset(ItemInfo, 0, 0xf00);
    memset(ItemStat, 0, 0x28);
    ItemNext = 0;
}
