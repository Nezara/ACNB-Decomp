/* src/enemy/enesfx.cpp (OLSORTIE) - enemy effect helpers.
 *   SFX_Dying: the "dying" spell 0x15e on a model when an AC or enemy starts dying (AC_add_damage, DFene_Kill).
 *   ene_boost_sfx / _mng / _delete / _init: the four booster flame effects of an enemy (h_sfx[] = 1-based Mag2St
 *     handles on the model vertices 16..19), switched on / off with the boost state, plus the booster sound.
 *   DFene_SFX / _flag / _NAP / _ECM: one-shot magic / spell effects on an enemy vertex or model.
 *   DFene_Bullet2 / 2Ex / 3: spawn an enemy weapon spell from a model vertex (AppearSpell2).
 *   enesfx_tank_spark / common_spark_mng: tank tread / leg sparks and water splashes keyed on the floor attribute.
 * Left interpreted: Enesfx_GetDiscernR2VtxID, DF_ENEMY_BOMB_SCALE, DFene_SFX_Boost (interpreted callers only). */
#include "acnb/port.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"
#include "acnb/sdk.h"

#define ESFX_015   0x1.333334p-3f   /* 0x3e19999a 0.15 */
#define ESFX_02    0x1.99999ap-3f   /* 0x3e4ccccd 0.2 */
#define ESFX_03    0x1.333334p-2f   /* 0x3e99999a 0.3 */
#define ESFX_PI    0x1.921fb6p+1f   /* 0x40490fdb */
#define ESFX_2PI   0x1.921fb6p+2f   /* 0x40c90fdb */
#define ESFX_NPI  -0x1.921fb6p+1f   /* 0xc0490fdb */

int SFX_Dying(StModel *mp, int frm)
{
    PS2_FRAME(0x50);   /* stack arguments 9-12 of DF_SPELL2_AppearSpell at sp+0..0x18 */
    _spell2 *ap = DF_SPELL2_AppearSpell(0x15e, 0, 0, 0, 0, 0, 0, 0, 0, mp, 0, 0);
    if (!ap) return 0;
    DF_SPELL2_SetFrame(ap, frm);
    return ap->on;
}

/* handle h (1-based) -> effect, NULL for h <= 0 */
static inline _magic2_st *esfx_mag(short h)
{
    return h > 0 ? &Mag2St[h] : 0;
}

void ene_boost_sfx(EnemySt *ep, int vhid)
{
    PS2_FRAME(0x40);
    StModel *mp = ep->mp;
    int on;
    if (ep->eff.nowbst) {
        enese_call_boost(ep, vhid);
        if (!ep->eff.oldbst) ene_ac_se_jet(ep, mp);
        on = 1;
    } else {
        DFene_SEStop(ep, vhid);
        on = 0;
    }
    for (int i = 0; i < 4; i++) {
        _magic2_st *m = esfx_mag(ep->h_sfx[i]);
        if (m) DF_MAGIC2_Boost(m, on);
    }
}

void ene_boost_mng(_magic2_st *magp, int flag)
{
    PS2_FRAME(0x10);
    if (flag) flag = 1;
    if (magp) DF_MAGIC2_Boost(magp, flag);
}

void ene_boost_sfx_delete(EnemySt *ep)
{
    PS2_FRAME(0x20);
    for (int i = 0; i < 4; i++) {
        DF_MAGIC2_Kill(esfx_mag(ep->h_sfx[i]));   /* NULL too (Kill returns on NULL) */
        ep->h_sfx[i] = 0;
    }
}

/* one booster flame on vertex vtx; the new handle (0 = none) */
static short esfx_boost_one(EnemySt *ep, int vtx, int mid, unsigned short option)
{
    ObjCMatSt *cm = DFene_GetOBJCMAT(ep->id, vtx);
    if (!cm || !cm->parentmatp) return 0;
    _magic2_st *m = DF_MAGIC2_AppearBoost((unsigned short)mid, cm, 0, option);
    return m ? m->on : 0;
}

/* mp unused; the handles keep their old value when boost / subboost <= 0 */
void ene_boost_sfx_init(EnemySt *ep, StModel *mp, int boost, int subboost)
{
    PS2_FRAME(0x40);
    (void)mp;
    if (boost > 0) {
        ep->h_sfx[0] = esfx_boost_one(ep, 0x10, boost, 4);
        ep->h_sfx[1] = esfx_boost_one(ep, 0x11, boost, 0x204);
    }
    if (subboost > 0) {
        ep->h_sfx[2] = esfx_boost_one(ep, 0x12, subboost, 4);
        ep->h_sfx[3] = esfx_boost_one(ep, 0x13, subboost, 0x204);
    }
}

void DFene_SFX_NAP(StModel *mp)
{
    PS2_FRAME(0x30);   /* stack arguments 9-12 at sp+0..0x18 */
    DF_SPELL2_AppearSpell(0x102, 0, 0, 0, 0, 0, 0, 0, 0, mp, 0, 0);
}

void DFene_SFX_ECM(StModel *mp)
{
    PS2_FRAME(0x30);
    DF_SPELL2_AppearSpell(0x103, 0, 0, 0, 0, 0, 0, 0, 0, mp, 0, 0);
}

/* the GetOBJCMAT result is dereferenced without a NULL test (as the original) */
int DFene_SFX(EnemySt *ep, int sfxid, int vtxid)
{
    PS2_FRAME(0x20);
    ObjCMatSt *cm = DFene_GetOBJCMAT(ep->id, vtxid);
    if (!cm->parentmatp) return 0;
    return DF_MAGIC2_AppearMagicOCMP((unsigned short)sfxid, cm, 0);
}

int DFene_SFX_flag(EnemySt *ep, int sfxid, int vtxid, int flag)
{
    PS2_FRAME(0x30);
    ObjCMatSt *cm = DFene_GetOBJCMAT(ep->id, vtxid);
    if (!cm->parentmatp) return 0;
    return DF_MAGIC2_AppearMagicOCMP((unsigned short)sfxid, cm, (unsigned short)flag);
}

/* one spell per vertex of wep->vtx_id (low nibble = first vertex, high nibble = count); loc is forwarded as is
 * (the spell copies all four lanes and tests target[3]) */
void DFene_Bullet3(EnemySt *ep, EneWepSt *wep, float *loc, StModel *lkmp)
{
    PS2_FRAME(0x80);
    if (!ep) return;
    if (ep->use & 0xc0) return;
    int vtx = wep->vtx_id & 0xf;
    int cnt = (wep->vtx_id >> 4) & 0xf;
    for (; cnt != 0; cnt--) {
        unsigned short spl = wep->spl_id;
        ObjCMatSt *cm = DFene_GetOBJCMAT(ep->id, vtx);
        vtx++;
        if (g_sortie.pwork->isPlayPolyg == 0 && (ep->use & 0xc0)) continue;
        if (!cm->parentmatp) continue;
        DF_SPELL2_AppearSpell2((short)spl, 1, (char)ep->id, &wep->dstat, cm, loc, lkmp);
    }
}

_spell2 *DFene_Bullet2(EnemySt *ep, int splid, int vtxid, float *loc, StModel *lkmp, SfxDstat *dstat)
{
    PS2_FRAME(0x60);
    ObjCMatSt *cm = DFene_GetOBJCMAT(ep->id, vtxid);
    if (g_sortie.pwork->isPlayPolyg == 0 && (ep->use & 0xc0)) return 0;
    if (!cm->parentmatp) return 0;
    return DF_SPELL2_AppearSpell2((short)splid, 1, (char)ep->id, dstat, cm, loc, lkmp);
}

/* as DFene_Bullet2, but the vertex belongs to enemy eid; the shooter id stays ep->id */
_spell2 *DFene_Bullet2Ex(EnemySt *ep, int eid, int splid, int vtxid, float *loc, StModel *lkmp, SfxDstat *dstat)
{
    PS2_FRAME(0x60);
    ObjCMatSt *cm = DFene_GetOBJCMAT(eid, vtxid);
    if (g_sortie.pwork->isPlayPolyg == 0 && (ep->use & 0xc0)) return 0;
    if (!cm->parentmatp) return 0;
    return DF_SPELL2_AppearSpell2((short)splid, 1, (char)ep->id, dstat, cm, loc, lkmp);
}

void enesfx_tank_spark(EnemySt *ep)
{
    PS2_FRAME(0x20);
    int id = 0;
    unsigned int div = 1;
    /* floor below the water line reads the yuka (floor) attribute, else the mizu (water) one: looks inverted, kept */
    unsigned char attr = (ep->floor_Y <= ep->water_ht) ? ep->mp->yukaattribute : ep->mp->mizuattribute;
    switch (attr) {
    case 1: id = 0x184; div = 10; break;
    case 2: id = 0; div = 10; break;
    case 4:
        if (ep->bst == 6 || ep->dir != 8) { id = 0x17a; div = 10; }
        break;
    default: break;
    }
    if (id == 0) return;
    if (FRAME % div != 0) return;
    ObjCMatSt *cm = DFene_GetOBJCMAT(ep->id, (int)((FRAME / div) & 3) + 0x14);
    if (cm->parentmatp) DF_MAGIC2_AppearMagicOCMP((unsigned short)id, cm, 0);
}

/* splash magic 0x184 on vertex k + 0x13 */
static void esfx_splash(EnemySt *ep, int k)
{
    ObjCMatSt *cm = DFene_GetOBJCMAT(ep->id, k + 0x13);
    if (cm->parentmatp) DF_MAGIC2_AppearMagicOCMP(0x184, cm, 0);
}

/* spark spell at vertex k + 0x13, yawed against the enemy's horizontal velocity */
static void esfx_spark(EnemySt *ep, int k, int id, int flag)
{
    ObjCMatSt *cm = DFene_GetOBJCMAT(ep->id, k + 0x13);
    if (!cm->parentmatp) return;
    float pos[4] __attribute__((aligned(16)));
    float vel[4] __attribute__((aligned(16)));
    float ang[4] __attribute__((aligned(16)));
    vu_apply(pos, (const float (*)[4])*cm->parentmatp, cm->mat[3]);
    vel[0] = ep->vel[0]; vel[1] = ep->vel[1]; vel[2] = ep->vel[2]; vel[3] = ep->vel[3];
    float vx = ep->vel[0], vz = ep->vel[2];
    float spd = ps2_sqrt(vx * vx + vz * vz);   /* mula.s / madd.s / sqrt.s */
    if (spd < ESFX_02) return;
    if (spd < ESFX_03) { vel[0] = -vx; vel[2] = -ep->vel[2]; }
    ang[0] = 0.0f;
    float a = ESFX_PI + ps2_atan2f(vel[0], vel[2]);
    if (!(a <= ESFX_PI)) {
        do { a = a - ESFX_2PI; } while (!(a <= ESFX_PI));
    }
    if (a < ESFX_NPI) {
        do { a = a + ESFX_2PI; } while (a < ESFX_NPI);
    }
    ang[1] = a;
    ang[2] = 0.0f;
    ang[3] = 1.0f;
    DF_SPELL2_AppearSpell((short)id, 0, 0, 0, 0, pos, ang, 0, 0, 0, (unsigned short)flag, 0);
}

/* the floor attribute is read from ep->mp, not the mp argument (as the original); legct = number of legs,
 * leg k (1-based) uses vertex k + 0x13; mask 10 = every 10th frame per leg */
void common_spark_mng(EnemySt *ep, StModel *mp, int legct, int water)
{
    PS2_FRAME(0x1a0);   /* stack arguments of DF_SPELL2_AppearSpell at sp+0..0x18 */
    switch (ep->bst) {
    case 1: case 2: case 4: case 5: break;
    default: return;
    }
    int mask = 1;
    float wh = 0.0f;
    if (water) wh = ene_get_water_high(mp, mp->loc);
    unsigned char yk = ep->mp->yukaattribute;
    if (yk == 0) {
        if (!water) mask = 1;
        else if (mp->loc[1] <= wh - 10.0f) mask = 1;
        else if (ep->floor_Y <= wh) mask = 1;
        else if (wh <= mp->loc[1] - 0.5f * mp->tall) mask = 1;
        else mask = 0xa;
    } else if (yk <= 5) {
        mask = 0xa;
    }
    for (int k = 1; k <= legct; k++) {
        if ((unsigned int)(k & mask) != (FRAME & (unsigned int)mask)) continue;
        float f1;
        switch (ep->mp->yukaattribute) {
        case 0:
            if (!water) {
                if (mp->loc[1] <= ep->floor_Y - ESFX_015) break;
                esfx_spark(ep, k, 0x17b, 2);
                break;
            }
            f1 = mp->loc[1];
            if (f1 <= wh - 10.0f) {
                if (f1 <= ep->floor_Y - ESFX_015) break;
                esfx_spark(ep, k, 0x17b, 2);
                break;
            }
            if (ep->floor_Y <= wh) break;
            if (wh <= f1 - 0.5f * mp->tall) break;
            esfx_splash(ep, k);
            break;
        case 1:
            if (!water) break;
            f1 = mp->loc[1];
            if (f1 <= wh - 10.0f) break;
            if (ep->floor_Y <= wh) break;
            if (wh <= f1 - 0.5f * mp->tall) break;
            esfx_splash(ep, k);
            break;
        case 4:
            if (mp->loc[1] <= ep->floor_Y - ESFX_015) break;
            esfx_spark(ep, k, 0x17a, 0);
            break;
        default:
            break;
        }
    }
}
