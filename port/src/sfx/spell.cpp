/* src/sfx/spell.c (Versus part) - "spell2" projectiles: up to 511 live spells in sap0[1..511] (slot 0 unused by
 * the allocator), each driven by a _stSpell2Data record (SpellData2 or a combined sfx file) whose pid selects a
 * program in s_SpellFunc {init, main, hit, kill}. DF_SPELL2_AppearSpell(2)(Ptr) spawn `with` copies of a spell,
 * DF_SPELL2_SpellProc runs the main program of every live spell once per frame (after a spell's own frame), and
 * collision reports hits through the hit program, which calls __spell_Hit (damage with distance / lifetime
 * decrease, enemy / AC / spell / map hit handling, hit spells and sparks). Killed spells pass through on = -2, -1
 * before their slot is freed.
 * Programs ported here: 01 (static sphere), 02/03 (bullets with line pre-check against the map), 04 (missiles:
 * homing, decoys, stray), 07 (decoy flares), 08 (anti-decoy missiles), 10 (missile salvo carrier + missile_fire),
 * 15 (funnel: fly to a slot around the target, hover and fire; DF_SPELL2_Funnel_Init shared with 16),
 * 17 (dummy target / ECM sphere: MainProg17_Jam stays interpreted), 18 (model-attached sparks), 23 (object sfx);
 * the other programs stay interpreted and are reached through s_SpellFunc.
 * Quirks: __spell_InitProg07 / MainProg07 leave speed = 1 / length after normalising n_speed towards the decoy;
 * InitProg01 clears flag.kind / flag.id (the shooter) after spawning its effects. */
#include "acnb/sdk.h"
#include "acnb/vu0m.h"

#ifdef ACNB_SOURCEPORT
unsigned short s_ParabolicID = 1;   /* 0x27b010 */
#endif

static const float k_pi = ps2_u2f(0x40490fdb), k_npi = ps2_u2f(0xc0490fdb), k_2pi = ps2_u2f(0x40c90fdb);
static const float k_pi2 = ps2_u2f(0x3fc90fdb), k_npi2 = ps2_u2f(0xbfc90fdb);
static const float k_deg = ps2_u2f(0x3c8efa35);   /* pi / 180 */

static_assert(sizeof(_spell2) == 0x120 && sizeof(_stSpell2Data) == 0xc0 && sizeof(StModel) == 0x180, "layout");
static_assert(sizeof(ObjectSt) == 0x80 && sizeof(AcPlayerSt) == 0x2600, "layout");

/* ---------------------------------------------------------------- helpers */

static void copy4(float *d, const float *s) { memcpy(d, s, 16); }

/* the angle wrap loops of the original ([-pi, pi]) */
static float wrap_pi(float a)
{
    while (!(a <= k_pi)) a = a - k_2pi;
    while (a < k_npi) a = a + k_2pi;
    return a;
}

/* (0x7fff - (rand & 0xffff)) / 32767: a random factor in [-1, 1] */
static float rnd_sym(void)
{
    unsigned int r = (unsigned int)ps2_rand() & 0xffff;
    return (float)(int)(0x7fff - r) / 32767.0f;
}

/* (rand & 0xffff) / 65536: [0, 1) */
static float rnd01(void) { return (float)(int)((unsigned int)ps2_rand() & 0xffff) / 65536.0f; }

/* rand() % 0x8000 < lim */
static int rand_pass(int lim) { return ps2_mod32(ps2_rand(), 0x8000) < lim; }

/* rand() % 0x190000 >> 14: 0..99 */
static int rand_pct(void) { return ps2_mod32(ps2_rand(), 0x190000) >> 14; }

/* float -> unsigned as MWCC converts it (values >= 2^31 through the signed range) */
static unsigned int f2u(float f)
{
    if (2147483648.0f <= f) return (unsigned int)ps2_ftoi(f - 2147483648.0f) | 0x80000000u;
    return (unsigned int)ps2_ftoi(f);
}

static float len3(const float *v) { return ps2_sqrt((v[0] * v[0] + v[1] * v[1]) + v[2] * v[2]); }

/* direction -> {pitch, yaw, 0, 1}: the inline idiom of the original (both the mul/add and the mula/madd form);
 * not lib/calc vec2ang */
static void dir2ang(float *ang, const float *v)
{
    float hz = ps2_sqrt(v[0] * v[0] + v[2] * v[2]);
    float ax;
    if (hz == 0.0f) ax = v[1] < 0.0f ? k_npi2 : v[1] <= 0.0f ? 0.0f : k_pi2;
    else ax = ps2_atan2f(v[1], hz);
    float ay = ps2_atan2f(v[0], v[2]);
    ang[0] = ax; ang[1] = ay; ang[2] = 0.0f; ang[3] = 1.0f;
}

static void kill(_spell2 *ap) { gcall(s_SpellFunc[ap->sdp->pid].killfunc, ap); }

/* matrix = RotZ(ang z) * RotX(-ang x) * RotY(ang y) + pos */
static void spell_matrix(_spell2 *ap)
{
    sceVu0RotMatrixZ(ap->matrix, IdentityMtx, ap->ang[2]);
    sceVu0RotMatrixX(ap->matrix, ap->matrix, -ap->ang[0]);
    sceVu0RotMatrixY(ap->matrix, ap->matrix, ap->ang[1]);
    sceVu0TransMatrix(ap->matrix, ap->matrix, ap->pos);
}

/* ang x / y += er_ang * random + hosei (degrees), then wrapped */
static void spell_er(_spell2 *ap)
{
    float r = rnd_sym();
    ap->ang[0] = ap->ang[0] + (ap->spell->er_ang_x * r + k_deg * ap->sdp->hosei[0]);
    r = rnd_sym();
    ap->ang[1] = ap->ang[1] + (ap->spell->er_ang_y * r + k_deg * ap->sdp->hosei[1]);
    ap->ang[0] = wrap_pi(ap->ang[0]);
    ap->ang[1] = wrap_pi(ap->ang[1]);
}

/* rot = n_speed * k (vmulx.xyz: rot.w = n_speed.w) */
static void scale_rot(_spell2 *ap, float k)
{
    ap->rot[0] = ap->n_speed[0] * k; ap->rot[1] = ap->n_speed[1] * k; ap->rot[2] = ap->n_speed[2] * k;
    ap->rot[3] = ap->n_speed[3];
}

static void spell_rot(_spell2 *ap) { scale_rot(ap, ap->speed); }

static void add3(float *d, const float *s) { d[0] = d[0] + s[0]; d[1] = d[1] + s[1]; d[2] = d[2] + s[2]; }

static _magic2_data_st *comb_mdp(_spell2 *ap, int mid)
{
    unsigned int base = DF_MAGIC2_GetMagicReduce() ? ap->comb->magic_ofs2 : ap->comb->magic_ofs;
    return (_magic2_data_st *)(uintptr_t)(base + mid * 0x140);
}

/* AppearMagic(Ptr)(mid, pos, ang, 0, 0, 0, 0, opt) */
static void magic_at(_spell2 *ap, int mid, float *pos, float *ang, int opt)
{
    if (ap->comb) DF_MAGIC2_AppearMagicPtr(comb_mdp(ap, mid), ap->comb, pos, ang, 0, 0, 0, 0, opt);
    else DF_MAGIC2_AppearMagic(mid, pos, ang, 0, 0, 0, 0, opt);
}

/* AppearMagic_SPL2(Ptr)(mid, pos, ang, 0, 0, mat0, n_speed, opt, &ap->on) */
static int magic_spl2(_spell2 *ap, int mid, float *pos, float *ang, float (*mat0)[4][4], float (*ns)[4], int opt)
{
    if (ap->comb)
        return DF_MAGIC2_AppearMagic_SPL2Ptr(comb_mdp(ap, mid), ap->comb, pos, ang, 0, 0, mat0, ns, opt, &ap->on);
    return DF_MAGIC2_AppearMagic_SPL2((unsigned short)mid, pos, ang, 0, 0, mat0, ns, opt, &ap->on);   /* lhu mid (lh for Ptr) */
}

static void magic_ocmp(_spell2 *ap, int mid, int opt)
{
    if (ap->comb) DF_MAGIC2_AppearMagicOCMPPtr(comb_mdp(ap, mid), ap->comb, ap->ocmp, opt);
    else DF_MAGIC2_AppearMagicOCMP(mid, ap->ocmp, opt);
}

/* a hit / follow-up spell of ap: AppearSpellPtr(spell sid of ap's file, kind, id, dstat, 0, comb, pos, ang,
 * 0, 0, 0, 0, ap->weapon_id); only the SpellData2 path skips sid 0 */
static _spell2 *spell_spawn(_spell2 *ap, int sid, int kind, int id, SfxDstat *dstat, float *pos, float *ang)
{
    if (ap->comb)
        return DF_SPELL2_AppearSpellPtr((_stSpell2Data *)(uintptr_t)(ap->comb->spell_ofs + sid * 0xc0), (char)kind,
                                        (char)id, dstat, 0, ap->comb, pos, ang, 0, 0, 0, 0, ap->weapon_id);
    if (!(short)sid) return 0;
    return DF_SPELL2_AppearSpellPtr(SpellData2 + (short)sid, (char)kind, (char)id, dstat, 0, 0, pos, ang, 0, 0, 0,
                                    0, ap->weapon_id);
}

/* ---------------------------------------------------------------- statistics */

static void __SpellHitSet(int s_type, int s_id, int flag)
{
    PS2_FRAME(0x30);
    if (s_type == 2) {
        if (s_id == 0x7f) return;
        Info_IncData(1, 3, s_id, 0);
        if (s_id) return;
        switch (flag & 0xf00) {
        case 0x400: Info_IncData(0, 0xe, 0, 0); break;
        case 0x600: Info_IncData(0, 0xd, 0, 0); break;
        case 0x500: Info_IncData(0, 0xc, 0, 0); break;
        case 0x300: Info_IncData(0, 0xb, 0, 0); break;
        case 0x200: Info_IncData(0, 0xa, 0, 0); break;
        case 0x100: Info_IncData(0, 9, 0, 0); break;
        }
    } else if (s_type == 1) {
        Info_IncData(2, 3, s_id, 0);
    }
}

static void __SpellFireSet(int s_type, int s_id, int flag)
{
    PS2_FRAME(0x30);
    if (s_type == 2) {
        if (s_id == 0x7f) return;
        Info_IncData(1, 2, s_id, 0);
        if (s_id) return;
        switch (flag & 0xf00) {
        case 0x400: Info_IncData(0, 5, 0, 0); break;
        case 0x600: Info_IncData(0, 4, 0, 0); break;
        case 0x500: Info_IncData(0, 3, 0, 0); break;
        case 0x300: Info_IncData(0, 2, 0, 0); break;
        case 0x200: Info_IncData(0, 1, 0, 0); break;
        case 0x100: Info_IncData(0, 0, 0, 0); break;
        }
    } else if (s_type == 1) {
        Info_IncData(2, 2, s_id, 0);
    }
}

void DF_SPELL2_SetFrame(_spell2 *ap, unsigned int frame) { ap->frame = (int)frame; }

/* kills every live weapon spell (s_SpellFunc kind != 0), slot 0 included */
void SPELL2_DeleteFire(void)
{
    PS2_FRAME(0x30);
    _spell2 *p = sap0;
    for (int i = 0; i < 0x200; i++, p++)
        if (p->on > 0 && s_SpellFunc[p->sdp->pid].kind) kill(p);
}

/* marks the spell's collision entry (num 0: hcol, else hcol2) as fired by its shooter */
static void __spell_SetSelfNum(_spell2 *ap, int num)
{
    PS2_FRAME(0x10);
    if (!ap) return;
    if (!num && ap->hcol < 0) return;
    if (num && ap->hcol2 < 0) return;
    int k = ap->flag.kind == 1 ? 3 : ap->flag.kind == 2 ? 4 : 0;
    if (!k) return;
    DF_COLL_SetSelf(num ? ap->hcol2 : ap->hcol, k, ap->flag.id);
}

/* line check of this frame's movement against the map (HetProcLine): sets maphit / map_hit, *dist = hit distance,
 * 0 for no hit, -1 when the hit point lies inside the box spanned by the start and the current position */
static void __spell_CheckHitToObjects(_spell2 *ap, float L, float *dist, float back)
{
    PS2_FRAME(0x80);
    if (!ap) return;
    float start[4], out[4];
    if (back != 0.0f) {
        float nb = -back;
        start[0] = ap->n_speed[0] * nb + ap->pos[0];
        start[1] = ap->n_speed[1] * nb + ap->pos[1];
        start[2] = ap->n_speed[2] * nb + ap->pos[2];
        start[3] = ap->n_speed[3];
    } else {
        copy4(start, ap->pos);
    }
    ap->hit_attr = (unsigned char)HetProcLine(start, ap->n_speed, L, out, 1);
    if (!*(unsigned int *)PS2_SPR(0)) {
        *dist = 0.0f;
        return;
    }
    ap->maphit = (unsigned short)(*(unsigned char *)PS2_SPR(7) << 12 | (*(unsigned short *)PS2_SPR(8) + 1));
    *dist = *(float *)PS2_SPR(0x70);
    ap->dat.gun.map_hit[0] = out[0]; ap->dat.gun.map_hit[1] = out[1]; ap->dat.gun.map_hit[2] = out[2];
    float mx[3], mn[3];
    for (int i = 0; i < 3; i++) {
        if (ap->pos[i] <= start[i]) { mx[i] = start[i]; mn[i] = ap->pos[i]; }
        else { mx[i] = ap->pos[i]; mn[i] = start[i]; }
    }
    if (mn[0] <= out[0] && out[0] <= mx[0] && mn[1] <= out[1] && out[1] <= mx[1] && mn[2] <= out[2] &&
        out[2] <= mx[2])
        *dist = -1.0f;
}

/* ---------------------------------------------------------------- hit */

/* result: hit type (bits 28-31: 3 enemy, 4 AC, 5 spell, else map part), up (24-27: hit_sid index for map hits),
 * object (0-15). Returns -1 when the hit is ignored. */
static int __spell_Hit(void *own, void *tgt, int result, _spell2 *ap, unsigned int start_hit_own, int start_hit_water)
{
    PS2_FRAME(0x180);
    PS2_STACK(SfxDstat, loc_d, 0x150);
    PS2_STACK(float[4], L, 0x170);
    int t = result >> 28 & 0xf, up = result >> 24 & 0xf, obj = result & 0xffff;
    int skind = 0, sid_ = 0;   /* shooter passed to the hit spells */
    SfxDstat *hdst = 0;
    int up1 = 0, sid = 0, sid2 = 0;
    _spell2 *p0 = 0, *res = 0;
    signed char isnorm = 0;
    unsigned char bhit = 0;

    memcpy(&loc_d, ap->dstat, 16);
    SfxDstat *old = ap->dstat;
    ap->dstat = &loc_d;

    /* damage decrease with the flight distance (missiles: lifetime) in percent */
    float pct = ap->stat.is_missile ? 100.0f * ps2_div((float)ap->frame, (float)ap->spell->lifetime)
                                    : 100.0f * ps2_div(ap->dist, ap->spell->range);
    if (!(pct <= 100.0f)) pct = 100.0f;
    _sfx_decrease_pow_st *dc = &loc_d.dec;
    if (!(dc->maxEnd < dc->maxStart)) {
        if (dc->maxStart > 0 && pct < (float)dc->maxStart) {
            float s = (float)dc->start, ms = (float)dc->maxStart;
            float acc = 0.0f * 100.0f;
            float a = pct * (100.0f - s);
            float b = acc - s * ms;
            float v = 0.01f * ps2_div(a - b, ms - 0.0f);
            loc_d.damage = (unsigned short)f2u((float)old->damage * v);
        } else if (dc->maxEnd > 0 && (float)dc->maxEnd < 100.0f && !(pct <= (float)dc->maxEnd)) {
            float me = (float)dc->maxEnd, e = (float)dc->end;
            float a = pct * (e - 100.0f);
            float b = me * e - 100.0f * 100.0f;
            float v = 0.01f * ps2_div(a - b, 100.0f - me);
            loc_d.damage = (unsigned short)f2u((float)old->damage * v);
        }
    }

    /* own shot hitting its shooter (before start_hit_own) / an enemy hitting itself or its attachments */
    if (obj == ap->flag.id && ((t == 4 && ap->flag.kind == 2) || (t == 3 && ap->flag.kind == 1)) &&
        (!start_hit_own || (unsigned int)ap->frame < start_hit_own)) {
        ap->dstat = old;
        return -1;
    }
    if (t == 3 && ap->flag.kind == 1) {
        EnemySt *ep = (EnemySt *)Chr[obj].arg0p;
        if (ap->flag.id == ep->attach[0] || ap->flag.id == ep->attach[1]) {
            ap->dstat = old;
            return -1;
        }
    }
    if (ap->flag.kind == 1) g_nEneHitBullet++;

    if (t == 3) {
        EnemySt *ep = (EnemySt *)Chr[obj].arg0p;
        unsigned int kills = CombatRecord_GetRecord()->killCnt[(int)ep->edat->kind];
        Enemy_SFXHit(ep, ap->dstat, ap->rot, ap->pos, ap->flag.kind, ap->flag.id);
        ep->hit_heat += ap->dstat->heat;
        if (ap->flag.kind == 2 && ap->flag.id != 0x7f) {
            if (!ep->sw.undead) g_ac[ap->flag.id].stat |= 1;
            if (ap->weapon_id >= 0xa1 && ap->weapon_id < 0xac &&
                kills < CombatRecord_GetRecord()->killCnt[(int)ep->edat->kind])
                SetBladeKillIncrease();
            CombatRecord_OnHit(&g_ac[ap->flag.id], ap->weapon_id, ap->dstat);
            __SpellHitSet(ap->flag.kind, ap->flag.id, ap->flags);
        }
        bhit = 1;
        sid = ap->sdp->hit_sid[6];
        ap->flag.sphere = 1;
    } else if (t == 4) {
        AcPlayerSt *acp = &g_ac[obj];
        if (ap->dstat->effect & 0x200) {
            PS2_STACK(float[4], v, 0x140);
            DF_COLL_GetHitLoc(ap->hcol, L);
            v[0] = L[0] - ap->pos[0]; v[1] = L[1] - ap->pos[1]; v[2] = L[2] - ap->pos[2]; v[3] = L[3];
            AcCtrl_HitAttack(acp, ap->dstat, v, ap->pos, ap->flag.kind, ap->flag.id);
        } else {
            AcCtrl_HitAttack(acp, ap->dstat, ap->rot, ap->pos, ap->flag.kind, ap->flag.id);
        }
        if (ap->flag.kind == 2 && ap->flag.id != 0x7f && acp != &g_ac[ap->flag.id]) {
            g_ac[ap->flag.id].stat |= 1;
            if (!obj) TrainingTrendIncreaseDamage();
            CombatRecord_OnHit(&g_ac[ap->flag.id], ap->weapon_id, ap->dstat);
        }
        sid = ap->sdp->hit_sid[6];
        if (ap->flag.kind == 1) {
            if (!obj) TrainingTrendIncreaseDamage();
            Ene_nHit++;
        }
        if (acp->mp) {
            acp->mp->shooter = (char)ap->flag.kind;
            acp->mp->shooterid = (unsigned char)ap->flag.id;
        }
        __SpellHitSet(ap->flag.kind, ap->flag.id, ap->flags);
        bhit = 1;
        ap->flag.sphere = 0;
    } else if (t == 5) {
        _spell2 *p = sap0 + 1;
        int i;
        for (i = 1; i < 0x200; i++, p++)
            if (p->on > 0 && p->hcol == obj) break;
        if (i >= 0x200 || ap == p) {
            ap->dstat = old;
            return -1;
        }
        p0 = p;
        int a = ap->sdp->pid, b = p0->sdp->pid;
        if (a == 7 || b == 7 || a == 8 || b == 8 || a == 1 || b == 1 || a == 0x16 || b == 0x16) {
            ap->dstat = old;
            return -1;
        }
        if (a == 9 || b == 9 || a == 0x11 || b == 0x11) {
            sid = ap->sdp->hit_sid[0];
            sid2 = p0->sdp->hit_sid[0];
            kill(p0);
        } else {
            sid = ap->sdp->hit_sid[0];
        }
    } else {
        int pid = ap->sdp->pid;
        if (pid == 0xd || pid == 0xe) {
            ap->dstat = old;
            return -1;
        }
        if (pid == 0xb && ap->sdp->params[0] && !ap->sdp->params[1]) {
            float n[4];
            DF_COLL_GetNorm(ap->hcol, n);
            dir2ang(ap->ang, n);
            isnorm = 1;
            hdst = old;
            ap->ang[0] = -ap->ang[0];
        } else {
            if (start_hit_water > 0 && ap->frame < start_hit_water && up == 1) {
                ap->dstat = old;
                return -1;
            }
            DF_SPELL2_MapHitProc_Param(t, obj, ap->pos, ap->flag.kind, ap->flag.id, ap->dstat);
        }
        sid = up < 7 ? ap->sdp->hit_sid[up] : 0;
        if (up == 1) up1 = 1;
    }

    if (sid) {
        if (ap->sdp->pid == 0x16) {
            DF_CALC_Angle2Vector(ap->n_speed, ap->ang);
            spell_rot(ap);
            float r = rnd_sym();
            L[0] = (ap->pos[0] + ap->rot[0] / 2.0f) + 7.5f * r;
            r = rnd_sym();
            L[1] = (ap->pos[1] + ap->rot[1] / 2.0f) + 5.0f * r;
            r = rnd_sym();
            L[3] = 1.0f;
            L[2] = (ap->pos[2] + ap->rot[2] / 2.0f) + 7.5f * r;
            res = spell_spawn(ap, sid, skind, sid_, hdst, L, 0);
        } else if (!ap->flag.sphere && ap->hcol >= 0 && DF_COLL_GetHitLoc(ap->hcol, L) == 0) {
            DF_COLL_GetHitLoc(ap->hcol, L);
            if (ap->comb) {
                sid_ = 0x7f;
                skind = ap->flag.kind;
            }
            res = spell_spawn(ap, sid, skind, sid_, hdst, L, up1 ? 0 : ap->ang);
            if (bhit == 1) DF_MAGIC2_AppearMagic(0x17, L, up1 ? ap->ang : 0, 0, 0, 0, 0, 0);
        } else if (ap->sdp->paramf[0] != 0.0f) {
            DF_COLL_GetHitLoc(ap->hcol, L);
            res = spell_spawn(ap, sid, skind, sid_, hdst, L, up1 ? ap->ang : 0);
            if (bhit == 1) DF_MAGIC2_AppearMagic(0x17, L, up1 ? ap->ang : 0, 0, 0, 0, 0, 0);
        } else {
            res = spell_spawn(ap, sid, skind, sid_, hdst, ap->matrix[3], up1 ? ap->ang : 0);
            if (bhit == 1) DF_MAGIC2_AppearMagic(0x17, ap->matrix[3], up1 ? ap->ang : 0, 0, 0, 0, 0, 0);
        }
    }
    if (sid2 && p0) spell_spawn(ap, sid2, skind, sid_, hdst, p0->matrix[3], up1 ? ap->ang : 0);
    if (isnorm && res && res->sdp->pid == 0xd) res->dat.adhesion.spell_id = ap->dat.parabolic.spell_id;
    ap->dstat = old;
    return 0;
}

static void __spell_HitProg24(void *own, void *tgt, int result)
{
    PS2_FRAME(0x20);
    _spell2 *ap = (_spell2 *)own;
    if (!(ap->on > 0)) return;
    if (__spell_Hit(own, tgt, result, ap, 0, 0) != -1) kill(ap);
}

/* AC hits only, once the spell's frame counter has run down; then it lives params[0] (5) more frames */
static void __spell_HitProg22(void *own, void *tgt, int result)
{
    PS2_FRAME(0x20);
    _spell2 *ap = (_spell2 *)own;
    if (!(ap->on > 0) || ap->frame > 0 || (result >> 28 & 0xf) != 4) return;
    if (__spell_Hit(own, tgt, result, ap, 0, 0) == -1) return;
    int n = ap->sdp->params[0];
    ap->frame = n > 0 ? n : 5;
}

static void __spell_HitProg21(void *own, void *tgt, int result)
{
    PS2_FRAME(0x20);
    _spell2 *ap = (_spell2 *)own;
    if (!(ap->on > 0)) return;
    if (__spell_Hit(own, tgt, result, ap, 0, ap->spell->start_hit) != -1) kill(ap);
}

static void __spell_HitProg19(void *own, void *tgt, int result)
{
    PS2_FRAME(0x20);
    _spell2 *ap = (_spell2 *)own;
    if (!(ap->on > 0) || ap->frame < ap->spell->start_hit) return;
    if (__spell_Hit(own, tgt, result, ap, 0, 0) != -1) kill(ap);
}

static void __spell_HitProg16(void *own, void *tgt, int result)
{
    PS2_FRAME(0x20);
    _spell2 *ap = (_spell2 *)own;
    if (!(ap->on > 0)) return;
    if (__spell_Hit(own, tgt, result, ap, ap->spell->start_hit, 0) != -1) kill(ap);
}

static void __spell_HitProg02(void *own, void *tgt, int result)
{
    PS2_FRAME(0x20);
    _spell2 *ap = (_spell2 *)own;
    if (!(ap->on > 0) || ap->frame < ap->spell->start_hit) return;
    if (__spell_Hit(own, tgt, result, ap, 0, 0) != -1) kill(ap);
}

/* the sphere hits only on its frames params[1] / params[2] and stays alive */
static void __spell_HitProg01(void *own, void *tgt, int result)
{
    PS2_FRAME(0x10);
    _spell2 *ap = (_spell2 *)own;
    if (!(ap->on > 0)) return;
    if (ap->frame == ap->sdp->params[1] || ap->frame == ap->sdp->params[2]) __spell_Hit(own, tgt, result, ap, 0, 0);
}

/* ---------------------------------------------------------------- missile steering */

static float aberr_rand(float rate)
{
    float r = (float)(int)((unsigned int)ps2_rand() & 0xffff) / 65535.0f;
    float v = rate * ((k_deg * r) / 8.0f);
    float r2 = (float)(int)((unsigned int)ps2_rand() & 0xffff) / 65535.0f;
    float w = rate * ((k_deg * r2) / 8.0f);
    return v < 0.0f ? -w : w;
}

/* random wobble of a missile: for stray_time frames n_speed is bent by stray (x, y, x), then new stray angles
 * (alternating sign, up_flag: y always flips) and a new stray_time in [min, max) */
static void __spell_Aberrance(_spell2 *ap, float param, int max_time, int min_time, int x, int y,
                              unsigned char up_flag)
{
    PS2_FRAME(0x70);
    if (!ap) return;
    if (!(0.0f < param) || !(param <= 8.0f)) return;
    if (!(min_time < max_time) || x < 2 || y < 2) return;
    SPELL2_MISSILE_ST *ms = &ap->dat.missile;
    if (ms->stray_time > 0) {
        float t[4];
        t[0] = ap->n_speed[0] + ms->stray[0];
        t[1] = ap->n_speed[1] + ms->stray[1];
        t[2] = ap->n_speed[2] + ms->stray[0];
        vu_normalize(ap->n_speed, t);
        ms->stray_time--;
        return;
    }
    float old = ms->stray[0];
    ms->stray[0] = aberr_rand(param);
    if (!(old < 0.0f)) ms->stray[0] = -ms->stray[0];
    old = ms->stray[1];
    ms->stray[1] = aberr_rand(param);
    if (!(old < 0.0f) || up_flag) ms->stray[1] = -ms->stray[1];
    int n = max_time - min_time;
    if (ps2_mod32(ps2_rand(), n) >= 0) ms->stray_time = (unsigned char)ps2_mod32(ps2_rand(), n);
    else ms->stray_time = (unsigned char)-ps2_mod32(ps2_rand(), n);
    ms->stray_time = (unsigned char)(ms->stray_time + (unsigned char)min_time);
}

/* the nearest decoy (this frame's s_pNowFrameDecoy, not the missile's own side) within 200 units and 30 degrees of
 * the flight direction: becomes the target (*loc too) */
static _spell2 *__spell_FindDecoy(float *loc, _spell2 *ap)
{
    if (!s_NowFrameDecoy) return 0;
    float dist[512];
    unsigned short idx[512];
    dist[0] = 0.0f;
    for (int i = 0; i < s_NowFrameDecoy; i++) {
        _spell2 *d = s_pNowFrameDecoy[i];
        if (d->flag.kind == ap->flag.kind && d->flag.id == ap->flag.id) {
            dist[i] = 0.0f;
            continue;
        }
        float v[3] = {d->pos[0] - ap->pos[0], d->pos[1] - ap->pos[1], d->pos[2] - ap->pos[2]};
        dist[i] = len3(v);
        if (!(dist[i] <= 200.0f)) dist[i] = 0.0f;
    }
    unsigned short n = 0;
    for (int i = 0; i < s_NowFrameDecoy; i++) {
        if (dist[i] != 0.0f) {
            dist[n] = dist[i];
            idx[n] = (unsigned short)i;
            n++;
        }
    }
    if (!n) return 0;
    for (int a = 0; a < n - 1; a++) {
        for (int b = a + 1; b < n; b++) {
            if (!(dist[a] <= dist[b])) {
                float f = dist[a]; dist[a] = dist[b]; dist[b] = f;
                unsigned short s = idx[a]; idx[a] = idx[b]; idx[b] = s;
            }
        }
    }
    const float lim = ps2_u2f(0x3f060a92);   /* 30 degrees */
    for (int i = 0; i < n; i++) {
        _spell2 *d = s_pNowFrameDecoy[idx[i]];
        float v[3] = {d->pos[0] - ap->pos[0], d->pos[1] - ap->pos[1], d->pos[2] - ap->pos[2]};
        float A[4], B[4];
        dir2ang(A, ap->n_speed);
        dir2ang(B, v);
        float tx = wrap_pi(B[0] - A[0]), ty = wrap_pi(B[1] - A[1]);
        if (tx < 0.0f) tx = -tx;
        if (!(tx < lim)) continue;
        if (ty < 0.0f) ty = -ty;
        if (!(ty < lim)) continue;
        copy4(ap->target, d->pos);
        copy4(loc, d->pos);
        return d;
    }
    return 0;
}

/* damage to a map part (STAGE model kind/id): hp, shooter, the owning character's and the model's message
 * functions */
void DF_SPELL2_MapHitProc_Param(int kind, int id, float *pos, int shooter_kind, int shooter_id, SfxDstat *dstat)
{
    PS2_FRAME(0x70);
    StModel *m = STAGE_GetModelPointer(kind, id);
    if (!m) return;
    if (m->nowhp > 0) {
        unsigned short a = m->array->relation;
        if (a && shooter_kind == 1 && (a & 0x3ff) == shooter_id) return;
        int dmg = dstat ? dstat->damage : 0;
        if (dmg > 0) m->nowhp = dmg < m->nowhp ? m->nowhp - dmg : 0;
        m->shooter = (char)shooter_kind;
        m->shooterid = (unsigned char)shooter_id;
        if (!(m->nowhp > 0)) m->nowhp = 0;
    }
    unsigned short a = m->array->relation;
    if (a) {
        StModel *c = &Chr[a & 0x3ff];
        if (c->msgfunc) gcall_as<void(void *, int, void *, void *)>(c->msgfunc, (void *)c, 4, (void *)m, (void *)dstat);
        if (shooter_kind == 2 && !(c->use & 4)) {
            g_ac[shooter_id].stat |= 1;
            __SpellHitSet(shooter_kind, shooter_id, 0);
        }
    }
    if (m->arg0p && m->msgfunc)
        gcall_as<void(void *, int, void *, void *)>(m->msgfunc, m->arg0p, 2, (void *)pos, (void *)dstat);
}

/* the smoke trail (Mag2St swk[]) follows the spell: spaced -|paramf[2]| along the movement, fading alpha */
static void __spell_SmokeProc(_spell2 *ap)
{
    PS2_FRAME(0x20);
    _stSpell2Data *sdp = ap->sdp;
    if (!sdp->sub_mid[0] || !ap->swk[0]) return;
    int n = sdp->params[1];
    if (!(n < 9)) n = 8;
    float f = ap->sdp->paramf[2];
    if (f < 0.0f) f = -f;
    float f4 = -f;
    float d[4];
    d[0] = ap->pos[0] - ap->dat.gun.pos1[0];
    d[1] = ap->pos[1] - ap->dat.gun.pos1[1];
    d[2] = ap->pos[2] - ap->dat.gun.pos1[2];
    if (d[0] == 0.0f && d[1] == 0.0f && d[2] == 0.0f) return;
    float dn[4];
    vu_normalize(dn, d);
    for (int i = 0; i < n; i++) {
        float s = f4 * (float)i;
        float v0 = dn[0] * s, v1 = dn[1] * s, v2 = dn[2] * s;
        _magic2_st *m = Mag2St + ap->swk[i];
        m->loc[0] = v0 + ap->matrix[3][0];
        m->loc[1] = v1 + ap->matrix[3][1];
        m->loc[2] = v2 + ap->matrix[3][2];
        m->loc[3] = 0.0f;
        float a = ps2_div(ap->sdp->paramf[1], (float)(ap->sdp->params[1] + 1)) * (float)(n - i + 1);
        (Mag2St + ap->swk[i])->rgba0[3] = (unsigned char)f2u(a);
    }
}

static void __spell_SmokeInit(_spell2 *ap)
{
    PS2_FRAME(0x70);
    _stSpell2Data *sdp = ap->sdp;
    if (!sdp->sub_mid[0]) return;
    float f = sdp->paramf[2];
    if (f < 0.0f) f = -f;
    int n = sdp->params[1];
    float f20 = -f;
    if (!(n < 9)) n = 8;
    unsigned int map = g_sortie.pconstant->map;
    if (!(map < 0x3c) && map < 0x64) n = 1;
    if (sdp->pid == 0x1e) n = 1;
    copy4(ap->dat.gun.pos1, ap->matrix[3]);
    for (int i = 0; i < n; i++) {
        float s = f20 * (float)i;
        float v[4];
        v[0] = ap->n_speed[0] * s + ap->matrix[3][0];
        v[1] = ap->n_speed[1] * s + ap->matrix[3][1];
        v[2] = ap->n_speed[2] * s + ap->matrix[3][2];
        v[3] = ap->n_speed[3];
        _magic2_st *mp = ap->comb ? DF_MAGIC2_AppearMagic2Ptr(comb_mdp(ap, ap->sdp->sub_mid[0]), ap->comb, v, ap->ang, 0)
                                  : DF_MAGIC2_AppearMagic2(ap->sdp->sub_mid[0], v, ap->ang, 0);
        if (!mp) return;
        ap->swk[i] = (unsigned short)mp->on;
    }
}

/* lead the decoy: *loc = decoy position extrapolated by the decoy's movement since the last target, deflect
 * (max 8) iterations of time-to-reach */
static void __spell_MissileHomingDecoy(float *loc, _spell2 *ap)
{
    int n = ap->spell->deflect;
    _spell2 *dc = ap->dat.missile.decoy;
    if (dc->on > 0) {
        if (n < 0 || !(n < 9)) n = 8;
        copy4(loc, dc->pos);
        float d[4];
        d[0] = loc[0] - ap->target[0]; d[1] = loc[1] - ap->target[1]; d[2] = loc[2] - ap->target[2];
        d[3] = 1.0f;
        while (n-- != 0) {
            float t[3] = {loc[0] - ap->pos[0], loc[1] - ap->pos[1], loc[2] - ap->pos[2]};
            float s = ps2_div(len3(t), ap->speed);
            loc[0] = dc->pos[0] + d[0] * s;
            loc[1] = dc->pos[1] + d[1] * s;
            loc[2] = dc->pos[2] + d[2] * s;
        }
        copy4(ap->target, dc->pos);
    } else if (dc->on < 0) {
        dc->on = -2;
        copy4(loc, dc->pos);
    } else {
        ap->dat.missile.decoy = 0;
        copy4(loc, ap->target);
    }
}

/* lead the target model (argp) the same way; looks for decoys first unless decoy_flag; warns the target (msgfunc
 * 1 within its vs_range, COM missile alert within 500) */
static void __spell_MissileHoming(float *loc, _spell2 *ap, int decoy_flag)
{
    PS2_FRAME(0xa0);
    StModel *m = (StModel *)ap->argp;
    int n = ap->spell->deflect;
    if (!decoy_flag) {
        ap->dat.missile.decoy = __spell_FindDecoy(loc, ap);
        if (ap->dat.missile.decoy) return;
    }
    if (m->use & 4) {
        copy4(loc, ap->target);
        ap->argp = 0;
        return;
    }
    float c[4];
    DF_GetModelCenter(c, m);
    copy4(loc, c);
    float d[4];
    d[0] = loc[0] - ap->target[0]; d[1] = loc[1] - ap->target[1]; d[2] = loc[2] - ap->target[2];
    d[3] = 1.0f;
    if (n < 0 || !(n < 9)) n = 8;
    while (n-- != 0) {
        float t[3] = {loc[0] - ap->pos[0], loc[1] - ap->pos[1], loc[2] - ap->pos[2]};
        float s = ps2_div(len3(t), ap->speed);
        loc[0] = c[0] + d[0] * s;
        loc[1] = c[1] + d[1] * s;
        loc[2] = c[2] + d[2] * s;
    }
    copy4(ap->target, c);
    float v[3] = {c[0] - ap->pos[0], c[1] - ap->pos[1], c[2] - ap->pos[2]};
    float f = len3(v);
    if (!ap->dat.missile.mis_assert && f <= m->vs_range) {
        ap->dat.missile.mis_assert = 1;
        if (m->msgfunc) gcall_as<void(void *, int, void *)>(m->msgfunc, (void *)m, 1, (void *)ap);
    }
    if (!ap->dat.missile.com_alert && f <= 500.0f && m->type == 4 && m->arg0p) {
        int cid = *(int *)m->arg0p;
        if (cid >= NowUserPlayerNum && !DF_COM_ExistMissile(cid - NowUserPlayerNum, ap)) {
            ap->dat.missile.com_alert = 1;
            DF_COM_AlertMissile(cid - NowUserPlayerNum, ap);
        }
    }
}

/* turn n_speed towards loc by trace */
static void spell_steer(_spell2 *ap, float *loc)
{
    loc[0] = loc[0] - ap->pos[0]; loc[1] = loc[1] - ap->pos[1]; loc[2] = loc[2] - ap->pos[2];
    vu_normalize(loc, loc);
    float tr = ap->spell->trace;
    loc[0] = loc[0] * tr; loc[1] = loc[1] * tr; loc[2] = loc[2] * tr;
    add3(ap->n_speed, loc);
    vu_normalize(ap->n_speed, ap->n_speed);
}

/* missile acceleration (limit max_speed * alter%); returns 1 when speed was touched */
static int spell_accel_alter(_spell2 *ap)
{
    float a = ap->spell->accel;
    if (!(a <= 0.0f)) {
        if (ap->frame < ap->spell->start_acc || ap->speed == ap->spell->max_speed) return 0;
        ap->speed = ap->speed + a;
        float lim = ps2_div(ap->spell->max_speed * (float)ap->alter, 100.0f);
        if (!(ap->spell->max_speed <= 0.0f) && !(ap->speed <= lim)) ap->speed = lim;
        return 1;
    }
    if (a < 0.0f) {
        if (ap->frame < ap->spell->start_acc || ap->speed <= 0.0f) return 0;
        ap->speed = ap->speed + a;
        if (ap->speed <= 0.0f) ap->speed = 0.0f;
        return 1;
    }
    return 0;
}

/* initial missile speed: MinSpeed% .. 100% of speed (alter = the percentage), else speed + (int)(trace * rnd) */
static void spell_init_speed(_spell2 *ap)
{
    if (ap->spell->MinSpeed) {
        float r = rnd01();
        float v = (float)(100 - ap->spell->MinSpeed) * r;
        ap->alter = (unsigned char)(f2u(v) + ap->spell->MinSpeed);
        if (ap->alter) {
            ap->speed = ps2_div(ap->spell->speed * (float)ap->alter, 100.0f);
        } else {
            float r3 = rnd_sym();
            int t = ps2_ftoi(ap->spell->trace * r3);
            ap->speed = ap->spell->speed + (float)t;
            if (t < 0) ap->alter = (unsigned char)ps2_ftoi(100.0f * ps2_div(ap->speed, ap->spell->speed));
            else ap->alter = 100;
        }
    } else {
        float r3 = rnd_sym();
        int t = ps2_ftoi(ap->spell->trace * r3);
        ap->speed = ap->spell->speed + (float)t;
        ap->alter = 100;
    }
}

/* the flight-effect magic every params[2]-th frame (sub_mid[1], option 8) */
static void spell_trail_magic(_spell2 *ap)
{
    int n = ap->sdp->params[2];
    if (n <= 0) n = 1;
    if (ap->sdp->sub_mid[1] > 0 && !ps2_mod32(ap->frame, n)) magic_at(ap, ap->sdp->sub_mid[1], ap->matrix[3], ap->ang, 8);
}

/* ang.xyz += the direction to the target (vadd.xyz: ang.w kept) */
static void spell_add_target_ang(_spell2 *ap)
{
    float d[3] = {ap->target[0] - ap->pos[0], ap->target[1] - ap->pos[1], ap->target[2] - ap->pos[2]};
    float a[4];
    dir2ang(a, d);
    add3(ap->ang, a);
}

/* pose + launch shared by InitProg17 / InitProg09 (the same block compiled twice): position from the ObjCMat
 * (one-row product with the parent for a target spell, full matrix product + MatrixZtoAng otherwise), angle
 * accumulated towards the target, error angles, speed = speed + trace * rnd (float, no truncation, no MinSpeed),
 * n_speed / rot / matrix. rand calls: 3. base_speed: InitProg11_sub's par_speed (the others pass spell->speed) */
/* the launch tail (also DF_SPELL2_Funnel_Init): error angles (rand #1, #2), speed (rand #3), n_speed / rot / matrix */
static void spell_launch_tail(_spell2 *ap, float base_speed)
{
    spell_er(ap);
    float r = rnd_sym();
    ap->speed = base_speed + ap->spell->trace * r;
    DF_CALC_Angle2Vector(ap->n_speed, ap->ang);
    spell_rot(ap);
    spell_matrix(ap);
}

static void spell_pose_launch_base(_spell2 *ap, float base_speed)
{
    ObjCMatSt *oc = ap->ocmp;
    if (oc) {
        if (ap->target[3] == 1.0f) {
            if (oc->parentmatp) vu_apply(ap->pos, *oc->parentmatp, oc->mat[3]);
            else copy4(ap->pos, oc->mat[3]);
            spell_add_target_ang(ap);
        } else {
            float M[4][4];
            if (oc->parentmatp) vu_mul_matrix(M, *oc->parentmatp, oc->mat);
            else sceVu0CopyMatrix(M, oc->mat);
            copy4(ap->pos, M[3]);
            MatrixZtoAng(ap->ang, M);
        }
    } else if (ap->target[3] == 1.0f) {
        spell_add_target_ang(ap);
    }
    spell_launch_tail(ap, base_speed);
}

static void spell_pose_launch(_spell2 *ap) { spell_pose_launch_base(ap, ap->spell->speed); }

/* ---------------------------------------------------------------- prog 24: two-entry collision shot */

/* program 24 (Main11 / Hit24 / Kill01): InitProg17's pose + launch (rand #1-#3), the SPL2 magic, a sub magic from
 * sub_mid[1] (sic: Init17 / Init09 use [0]) unless s_MazzleFlag, voice, then two collision entries - the first with
 * the constant HitProg16 (sic: not the table slot), the second with the table's hit function (HitProg24) - and the
 * sphere flag set without any SetSphr (sic); finally the horizontal speed into dat.parabolic.xz_speed. */
static void __spell_InitProg24(_spell2 *ap)
{
    PS2_FRAME(0xb0);
    spell_pose_launch(ap);   /* rand #1-#3 */
    ap->id = (unsigned short)magic_spl2(ap, ap->sdp->mid, 0, 0, &ap->matrix, 0, 4);
    if (!s_MazzleFlag && ap->sdp->sub_mid[1] > 0) {
        if (ap->ocmp && ap->ocmp->parentmatp) magic_ocmp(ap, ap->sdp->sub_mid[1], 4);
        else magic_at(ap, ap->sdp->sub_mid[1], ap->matrix[3], ap->ang, 0);
    }
    __spell_CallPointVoice(ap, 1);
    struct_88b6ff44 *sp = ap->spell;
    if (sp->se_id) ap->snd_id = (char)Voice_CallTraceLoc(sp->se_id, sp->se_vol, &ap->matrix[3], sp->se_dist);
    ap->hcol = (short)DF_COLL_AddList(ap, GFN(__spell_HitProg16), 1);
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        DF_COLL_SetVel(ap->hcol, ap->n_speed, ap->speed);
        ap->hcol2 = (short)DF_COLL_AddList(ap, s_SpellFunc[ap->sdp->pid].hitfunc, 1);
        if (ap->hcol2 >= 0) {
            DF_COLL_SetPos(ap->hcol2, ap->pos);
            DF_COLL_SetVel(ap->hcol2, ap->n_speed, ap->speed);
            DF_COLL_SetFlag(ap->hcol, 0x20, 1);
            DF_COLL_SetFlag(ap->hcol2, 0x50, 1);
            ap->flag.sphere = 1;
        }
    }
    ap->dat.parabolic.xz_speed = ps2_sqrt(ap->rot[0] * ap->rot[0] + ap->rot[2] * ap->rot[2]);   /* mula / madd */
}

/* ---------------------------------------------------------------- prog 23: object sfx */

static void __spell_KillProg23(_spell2 *ap) { __spell_KillSpellProc(ap); }

/* sparks (sdp->mid, option 4) on `with` random visible parts of the model */
static void DF_SPELL2_ModelOp_SFX(_spell2 *ap)
{
    PS2_FRAME(0xd0);
    StModel *mp = ap->dat.objsfx.mp;
    _stSpell2Data *sdp = ap->sdp;
    if (!mp || (mp->use & 1) || !(sdp->mid > 0) || !(sdp->with > 0)) return;
    for (int i = 0; i < sdp->with; i++) {
        int r = ps2_rand();
        ObjectSt *o = &mp->op[ps2_mod32(r, mp->nobj - 1) + 1];
        if (o->onoff) continue;
        if (sdp->params[0]) {
            ps2_rand();   /* a random angle is computed here but never passed on */
            ps2_rand();
        }
        if (ap->comb) DF_MAGIC2_AppearMagicPtr(comb_mdp(ap, sdp->mid), ap->comb, 0, 0, 0, 0, o->wrmatp, 0, 4);
        else DF_MAGIC2_AppearMagic(sdp->mid, 0, 0, 0, 0, o->wrmatp, 0, 4);
    }
}

static void __spell_MainProg23(_spell2 *ap)
{
    PS2_FRAME(0x30);
    int n = ap->sdp->params[1];
    if (n <= 0) n = 10;
    int go = 0;
    if (ap->sdp == SpellData2 + 0x101) go = rand_pass(0x667);
    else if (!(FRAME % (unsigned int)n)) go = 1;
    if (go) DF_SPELL2_ModelOp_SFX(ap);
    __spell_KillSpellProc(ap);
}

static void __spell_InitProg23(_spell2 *ap)
{
    PS2_FRAME(0x10);
    if (!ap->argp) kill(ap);
    else ap->dat.objsfx.mp = (StModel *)ap->argp;
}

/* ---------------------------------------------------------------- prog 18: sparks around a model */

static void __spell_KillProg18(_spell2 *ap) { __spell_KillSpellProc(ap); }

static void __spell_MainProg18(_spell2 *ap)
{
    PS2_FRAME(0x90);
    StModel *m = (StModel *)ap->argp;
    ap->frame--;
    sceVu0CopyMatrix(ap->matrix, ap->argp ? ((StModel *)ap->argp)->matrix2 : IdentityMtx);
    float L[4];
    vu_apply(L, ap->matrix, ap->pos);
    if (!(ap->frame > 0) && (!m || (m->use & 1))) {
        kill(ap);
        return;
    }
    if ((!m || m->type == 4 || m->type == 3) && !(ap->frame % 16))
        DF_MAGIC2_AppearMagic(ap->sdp->sub_mid[0], L, ap->ang, 0, 0, 0, 0, 8);
    int k = m && m->type != 4 ? 10 : 20;
    if (ap->frame <= 0) return;
    int r = ps2_mod32(ps2_rand(), 0x8000);
    if (k * 0x7fff / 100 < r) return;
    if (!m) return;
    float a = k_pi * (2.0f * rnd01() - 1.0f);
    float rad = m->R * rnd01();
    float v[4];
    v[0] = rad * fsmath_SinVU0(a);
    v[1] = -m->tall * rnd01();
    rad = m->R * rnd01();
    v[2] = rad * fsmath_CosVU0(a);
    const float k8 = ps2_u2f(0x3f4ccccd);
    v[0] = v[0] * k8; v[1] = v[1] * k8; v[2] = v[2] * k8;
    v[1] = v[1] - 5.0f;
    v[0] = L[0] + v[0]; v[1] = L[1] + v[1]; v[2] = L[2] + v[2]; v[3] = L[3];
    if (ap->sdp->mid > 0) magic_at(ap, ap->sdp->mid, v, 0, 0);
    if (!(FRAME < (unsigned int)(ap->dat.magic.before + 5)) && (ps2_mod32(ps2_rand(), 0x8000) >> 14)) {
        ap->dat.magic.before = (int)FRAME;
        __spell_CallPointVoice(ap, 1);
    }
}

static void __spell_InitProg18(_spell2 *ap)
{
    PS2_FRAME(0x60);
    sceVu0CopyMatrix(ap->matrix, ap->argp ? ((StModel *)ap->argp)->matrix2 : IdentityMtx);
    ap->frame = ap->spell->lifetime + 1;
    float r = rnd_sym();
    ap->ang[0] = ap->ang[0] + ap->spell->er_ang_x * r;
    r = rnd_sym();
    ap->ang[1] = ap->ang[1] + ap->spell->er_ang_y * r;
    ap->ang[0] = wrap_pi(ap->ang[0]);
    ap->ang[1] = wrap_pi(ap->ang[1]);
    float L[4];
    vu_apply(L, ap->matrix, ap->pos);
    for (int k = 0; k < 2; k++)
        if (ap->sdp->sub_mid[k] > 0) magic_at(ap, ap->sdp->sub_mid[k], L, ap->ang, 0);
}

/* ---------------------------------------------------------------- prog 08: anti-decoy missile */

static void __spell_MainProg08(_spell2 *ap)
{
    PS2_FRAME(0xa0);
    int chg = 0;
    ap->frame++;
    if (ap->spell->lifetime < ap->frame) {
        DF_MAGIC2_AppearMagic(0x30, ap->matrix[3], ap->ang, 0, 0, 0, 0, 0);
        kill(ap);
        return;
    }
    _spell2 *dc = ap->dat.missile.decoy;
    if (dc) {
        if (!dc->on) {
            ap->dat.missile.decoy = 0;
            ap->argp = 0;
        } else {
            if (dc->on < 0) dc->on = -2;
            dc = ap->dat.missile.decoy;
            float v[3] = {dc->pos[0] - ap->pos[0], dc->pos[1] - ap->pos[1], dc->pos[2] - ap->pos[2]};
            float len = len3(v);
            if (dc->on > 0 && len <= 5.0f && rand_pct() < ap->spell->deflect) {
                int h = ap->sdp->hit_sid[0];
                if (h > 0) spell_spawn(ap, h, 0, 0, 0, ap->matrix[3], ap->ang);
                kill(ap->dat.missile.decoy);
                kill(ap);
                g_nEneHitBullet++;
                return;
            }
        }
    }
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        DF_COLL_SetVel(ap->hcol, ap->n_speed, ap->speed);
    }
    add3(ap->pos, ap->rot);
    dc = ap->dat.missile.decoy;
    if (dc) {
        if (!dc->on) {
            ap->dat.missile.decoy = 0;
            ap->argp = 0;
        } else if (ap->spell->start_homing < ap->frame) {
            float loc[4];
            __spell_MissileHomingDecoy(loc, ap);
            spell_steer(ap, loc);
            chg = 1;
        }
    }
    if (spell_accel_alter(ap)) chg = 1;
    if (chg) spell_rot(ap);
    dir2ang(ap->ang, ap->n_speed);
    spell_matrix(ap);
    spell_trail_magic(ap);
    __spell_SmokeProc(ap);
    copy4(ap->dat.missile.pos1, ap->pos);
}

static void __spell_InitProg08(_spell2 *ap)
{
    PS2_FRAME(0xa0);
    int have = 0;
    float M[4][4];
    ObjCMatSt *oc = ap->ocmp;
    if (oc && oc->parentmatp) {
        vu_mul_matrix(M, *oc->parentmatp, oc->mat);
        copy4(ap->pos, M[3]);
        have = 1;
    }
    if (ap->target[3] == 1.0f) {
        float d[3] = {ap->target[0] - ap->pos[0], ap->target[1] - ap->pos[1], ap->target[2] - ap->pos[2]};
        dir2ang(ap->ang, d);
    } else if (have) {
        MatrixZtoAng(ap->ang, M);
    }
    spell_er(ap);
    spell_init_speed(ap);
    DF_CALC_Angle2Vector(ap->n_speed, ap->ang);
    spell_rot(ap);
    spell_matrix(ap);
    ap->id = (unsigned short)magic_spl2(ap, ap->sdp->mid, 0, 0, &ap->matrix, (float (*)[4])ap->n_speed, 4);
    __spell_CallPointVoice(ap, 1);
    struct_88b6ff44 *sp = ap->spell;
    if (sp->se_id) ap->snd_id = (char)Voice_CallTraceLoc(sp->se_id, sp->se_vol, &ap->matrix[3], sp->se_dist);
    ap->dat.missile.smoke_id = DF_MAGIC3_Appear((float (*)[4])ap->pos, 0, (float (*)[4])ap->rot, 0);
    ap->dat.missile.decoy = (_spell2 *)ap->argp;
    if (ap->dat.missile.decoy && ap->dat.missile.decoy->on <= 0) ap->dat.missile.decoy = 0;
    if (ap->dat.missile.decoy) copy4(ap->target, ap->dat.missile.decoy->pos);
    ap->hcol = (short)DF_COLL_AddList(ap, s_SpellFunc[ap->sdp->pid].hitfunc, 1);
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        DF_COLL_SetVel(ap->hcol, ap->n_speed, ap->speed);
        __spell_SetSelfNum(ap, 0);
    }
    __spell_SmokeInit(ap);
}

/* ---------------------------------------------------------------- prog 17: dummy target / ECM sphere */

static void __spell_KillProg17(_spell2 *ap)
{
    PS2_FRAME(0x20);
    SPELL2_MOVE_DUMMY_ST *md = &ap->dat.move_dummy;
    if (ap->sdp->paramf[2] != 2.0f) {
        for (int i = 0; i < 4; i++) {
            StModel *m = md->modelp[i];
            if (m) {
                m->cflag &= 0xdfff;
                md->modelp[i] = 0;
            }
        }
    }
    if (md->ecmhandle > 0) EcmField_UnRegisterEcm(md->ecmhandle);
    DF_COLL_Delete(ap->hcol);
    ap->hcol = -1;
    __spell_KillSpellProc(ap);
}

static void __spell_MainProg17_Dummy(void) {}

/* flies (accel from start_acc, or an aberrance wobble from frame params[0]) until lifetime < frame; paramf[2]
 * selects the mode (0 dummy, 2 flag.dummy, 3 jammer); paramf2[0] == 1: cylinder hit against player 0 */
static void __spell_MainProg17(_spell2 *ap)
{
    PS2_FRAME(0x80);
    PS2_STACK(float[4], v, 0x60);
    PS2_STACK(float[4], a, 0x70);
    signed char flag = 0;
    ap->frame++;
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        if (ap->hcol2 >= 0) {
            DF_COLL_SetPos(ap->hcol2, ap->pos);
            DF_COLL_SetVel(ap->hcol2, ap->n_speed, ap->speed);
        }
    }
    if (ap->spell->lifetime < ap->frame) {
        DF_MAGIC2_AppearMagic(0x30, ap->matrix[3], ap->ang, 0, 0, 0, 0, 0);
        kill(ap);
        return;
    }
    add3(ap->pos, ap->rot);
    _stSpell2Data *sdp = ap->sdp;
    int p0 = sdp->params[0];
    if (!(ap->frame < p0) && p0 != 0) {
        int maxt = sdp->params[1], mint = sdp->params[2];
        if (!(mint < maxt) || !maxt || !mint) {
            maxt = 0xb4;
            mint = 0x3c;
        }
        float r = ap->sdp->paramf[0];
        if (!(0.0f < r) || !(r <= 8.0f)) r = 8.0f;
        __spell_Aberrance(ap, r, maxt, mint, 0x2d, 2, 0);
        ap->n_speed[1] = ap->n_speed[1] / 2.5f;
        vu_normalize(ap->n_speed, ap->n_speed);
        flag = 1;
    } else if (!(ap->frame < ap->spell->start_acc)) {
        float acc = ap->spell->accel;
        if (!(acc <= 0.0f)) {
            ap->speed = ap->speed * acc;
            float mx = ap->spell->max_speed;
            if (!(mx <= 0.0f) && !(ap->speed <= mx)) ap->speed = mx;
            if (ap->speed < ps2_u2f(0x38d1b717)) ap->speed = 0.0f;
            flag = 1;
        }
    }
    if (flag) spell_rot(ap);
    dir2ang(a, ap->n_speed);
    copy4(ap->ang, a);
    spell_matrix(ap);
    float m = ap->sdp->paramf[2];
    if (m == 0.0f) __spell_MainProg17_Dummy();
    else if (m == 2.0f) ap->flag.dummy = 1;
    else if (m == 3.0f) __spell_MainProg17_Jam(ap);
    if (ap->sdp->paramf2[0] == 1.0f) {
        AcPlayerSt *pl = &g_ac[0];
        *(unsigned int *)PS2_SPR(0x50) = 0;
        DF_HIT_CylinderPoint(ap->pos, pl->loc, pl->tall, pl->R);   /* the asm passes tall as RR, R as tall */
        if (*(unsigned int *)PS2_SPR(0x50)) {
            v[0] = pl->loc[0] - ap->pos[0];
            v[1] = pl->loc[1] - ap->pos[1];
            v[2] = pl->loc[2] - ap->pos[2];
            v[3] = pl->loc[3];   /* vsub.xyz + full store */
            AcCtrl_HitAttack(pl, ap->dstat, v, ap->pos, ap->flag.kind, ap->flag.id);
            TrainingTrendIncreaseDamage();
            short hs = ap->sdp->hit_sid[0];
            if (hs)
                DF_SPELL2_AppearSpellPtr(SpellData2 + hs, (char)ap->flag.kind, (char)ap->flag.id, ap->dstat, 0, 0,
                                         ap->matrix[3], 0, 0, 0, ap->argp, ap->flags, ap->weapon_id);
            kill(ap);
        }
    }
}

static void __spell_InitProg17(_spell2 *ap)
{
    PS2_FRAME(0xb0);
    spell_pose_launch(ap);   /* rand #1-#3 */
    ap->id = (unsigned short)magic_spl2(ap, ap->sdp->mid, 0, 0, &ap->matrix, 0, 4);
    if (ap->sdp->sub_mid[0] > 0) {
        if (ap->ocmp && ap->ocmp->parentmatp) magic_ocmp(ap, ap->sdp->sub_mid[0], 4);
        else magic_at(ap, ap->sdp->sub_mid[0], ap->matrix[3], ap->ang, 0);
    }
    __spell_CallPointVoice(ap, 1);
    struct_88b6ff44 *sp = ap->spell;
    if (sp->se_id) ap->snd_id = (char)Voice_CallTraceLoc(sp->se_id, sp->se_vol, &ap->matrix[3], sp->se_dist);
    if (ap->sdp->paramf2[0] == 0.0f) ap->hcol = (short)DF_COLL_AddList(ap, s_SpellFunc[ap->sdp->pid].hitfunc, 1);
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        DF_COLL_SetSphr(ap->hcol, 3.0f);
        ap->flag.sphere = 1;
        ap->hcol2 = (short)DF_COLL_AddList(ap, s_SpellFunc[ap->sdp->pid].hitfunc, 1);
        if (ap->hcol2 >= 0) {
            DF_COLL_SetSelf(ap->hcol, 5, ap->hcol2);
            DF_COLL_SetSelf(ap->hcol2, 5, ap->hcol);
            DF_COLL_SetFlag(ap->hcol2, 0x10, 1);
            DF_COLL_SetPos(ap->hcol2, ap->pos);
            DF_COLL_SetVel(ap->hcol2, ap->n_speed, ap->speed);
        }
    }
    SPELL2_MOVE_DUMMY_ST *md = &ap->dat.move_dummy;
    md->stray[0] = rnd_sym();   /* rand #4 */
    md->stray[1] = -1.0f;
    int b = 0;
    switch (ap->flag.kind) {
    case 2: b = g_sortie.pconstant->player[ap->flag.id].belonging; break;
    case 1: b = STAGE_GetModelPointer(3, ap->flag.id)->array->belonging; break;
    case 0: return;
    default: break;
    }
    md->ecmhandle = EcmField_RegisterEcm(3, (unsigned char)b, 0, ap->spell->ecmnoise);
}

/* ---------------------------------------------------------------- prog 15: funnel (Funnel_Init also for prog 16) */

#ifdef ACNB_SOURCEPORT
/* the six funnel slots around the target (unit vectors, w 0), main 0x2606b0 */
float __spell_MainProg16__offset[6][4] = {
    {1.0f, -0.5f, 0.0f, 0.0f},
    {0.5f, -0.5f, ps2_u2f(0x3f5db3d7), 0.0f},
    {-0.5f, -0.5f, ps2_u2f(0x3f5db3d7), 0.0f},
    {-1.0f, -0.5f, 0.0f, 0.0f},
    {-0.5f, -0.5f, ps2_u2f(0xbf5db3d7), 0.0f},
    {0.5f, -0.5f, ps2_u2f(0xbf5db3d7), 0.0f},
};
#endif

/* the retarget idiom (4 inline copies in the original): slot = the target's loc - 2.5 up + offset[funnel_id] * 45,
 * slot angle from the offset direction; tmp = the original's stack temp of the scaled offset (w = offset.w) */
static void funnel_retarget(_spell2 *ap, float *tmp)
{
    SPELL2_FUNNEL_ST *fn = &ap->dat.funnel;
    const float *off = __spell_MainProg16__offset[fn->funnel_id];
    copy4(fn->tgt_pos, ((StModel *)ap->argp)->loc);
    fn->tgt_pos[1] = fn->tgt_pos[1] - 2.5f;
    tmp[0] = off[0] * 45.0f; tmp[1] = off[1] * 45.0f; tmp[2] = off[2] * 45.0f; tmp[3] = off[3];
    add3(fn->tgt_pos, tmp);
    float hz = ps2_sqrt(off[0] * off[0] + off[2] * off[2]);   /* mula / madd in Search / Init15, mul / add in Main15 */
    fn->tgt_ang[0] = ps2_atan2f(-off[1], hz);
    fn->tgt_ang[1] = ps2_atan2f(off[0], off[2]);
    fn->tgt_ang[1] = inverse_angf(fn->tgt_ang[1]);
}

/* the 0x30 end effect, collision entry 1 deleted, then the common kill. Not idempotent (Main15 can call it twice) */
static void __spell_KillProg15(_spell2 *ap)
{
    PS2_FRAME(0x20);
    DF_MAGIC2_AppearMagic(0x30, ap->matrix[3], ap->ang, 0, 0, 0, 0, 0);
    DF_COLL_Delete(ap->hcol);
    ap->hcol = -1;
    __spell_KillSpellProc(ap);
}

/* nearest lockable target within 150 in front (argp overwritten even when nothing is found), retarget the slot */
static int __spell_MainProg15_Search(_spell2 *ap)
{
    PS2_FRAME(0x50);
    PS2_STACK(float[4], tmp, 0x40);
    unsigned short mask = DF_SPELL_GetShooterUlockMask(ap->flag.kind, ap->flag.id);
    StModel *m = DF_get_nearest(ap->pos, ap->ang, 150.0f, k_pi2, k_pi, mask, ap->flag.kind, ap->flag.id);
    if (!m) {
        ap->argp = 0;
        return 0;
    }
    ap->argp = m;
    if (ap->argp) funnel_retarget(ap, tmp);   /* guard dead in the original too */
    ap->dat.funnel.phase = 0;
    return 1;
}

/* turn ang x / y a quarter of the way to tgt_ang; returns the number of axes already within 0.0017 */
static int __spell_MainProg15_setang(_spell2 *ap)
{
    PS2_FRAME(0x40);
    SPELL2_FUNNEL_ST *fn = &ap->dat.funnel;
    const float tol = ps2_u2f(0x3ae4c388);
    int n = 0;
    for (int i = 0; i < 2; i++) {
        float d = sub_angf(fn->tgt_ang[i], ap->ang[i]);
        float ad = d < 0.0f ? -d : d;
        if (ad <= tol) n++;
        else ap->ang[i] = go_angf(ap->ang[i], fn->tgt_ang[i], 0.25f * d);
        ap->ang[i] = wrap_pi(ap->ang[i]);
    }
    return n;
}

/* phase 0: fly to the slot (trace_fv, frame not counted) until both angles and the position (0.1) are there;
 * phase 1: hover - near the target (< 150) hop to the slot again every params[0] frames or face the target, fire
 * sub_sid every params[1] frames; too far: retarget + phase 0; a model with use & 4: Search, kill on failure and
 * fall through (sic: the lifetime test can kill a second time). SetVel to hcol2 only. */
static void __spell_MainProg15(_spell2 *ap)
{
    PS2_FRAME(0xe0);
    PS2_STACK(float[4], tmpB, 0x80);
    PS2_STACK(float[4], av, 0x90);
    PS2_STACK(float[4], tmpA, 0xa0);
    PS2_STACK(float[4], d, 0xb0);
    PS2_STACK(float[4], old, 0xc0);
    PS2_STACK(float[4], d0, 0xd0);
    StModel *model = (StModel *)ap->argp;
    SPELL2_FUNNEL_ST *fn = &ap->dat.funnel;
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        if (ap->hcol2 >= 0) DF_COLL_SetPos(ap->hcol2, ap->pos);
    }
    if (fn->phase == 0) {
        int n = __spell_MainProg15_setang(ap);
        copy4(old, ap->pos);
        trace_fv(ap->pos, fn->tgt_pos, ap->spell->speed);
        vu_sub_xyz(d0, ap->pos, fn->tgt_pos);
        if (len3(d0) < ps2_u2f(0x3dcccccd)) n++;
        vu_sub_xyz(ap->rot, ap->pos, old);   /* rot.w = pos.w */
        vu_normalize(ap->n_speed, ap->rot);
        ap->speed = len3(ap->rot);
        if (n == 3) fn->phase = 1;
    } else {
        ap->frame++;
        if (model) {
            if (model->use & 4) {
                if (!__spell_MainProg15_Search(ap)) kill(ap);   /* falls through (sic) */
            } else {
                vu_sub_xyz(d, ap->pos, model->loc);
                if (len3(d) < 150.0f) {
                    if (!ps2_mod32(ap->frame, ap->sdp->params[0])) {
                        if (ap->argp) funnel_retarget(ap, tmpA);
                        fn->phase = 0;
                    } else {
                        vu_normalize(d, d);
                        float hz = ps2_sqrt(d[0] * d[0] + d[2] * d[2]);
                        fn->tgt_ang[0] = ps2_atan2f(-d[1], hz);
                        fn->tgt_ang[1] = ps2_atan2f(d[0], d[2]);
                        fn->tgt_ang[1] = inverse_angf(fn->tgt_ang[1]);
                        __spell_MainProg15_setang(ap);
                    }
                    if (!ps2_mod32(ap->frame, ap->sdp->params[1])) {
                        ang2vec(av, wrap_pi(k_pi + ap->ang[1]), ap->ang[0]);   /* result unused (sic) */
                        _stSpell2Data *sdp = ap->sdp;
                        if (ap->comb)
                            DF_SPELL2_AppearSpellPtr((_stSpell2Data *)(uintptr_t)(ap->comb->spell_ofs + sdp->sub_sid * 0xc0),
                                                     (char)ap->flag.kind, (char)ap->flag.id, ap->dstat, 0, ap->comb,
                                                     ap->pos, ap->ang, 0, 0, 0, ap->flags, ap->weapon_id);
                        else if (sdp->sub_sid)
                            DF_SPELL2_AppearSpellPtr(SpellData2 + sdp->sub_sid, (char)ap->flag.kind, (char)ap->flag.id,
                                                     ap->dstat, 0, 0, ap->pos, ap->ang, 0, 0, 0, ap->flags,
                                                     ap->weapon_id);
                    }
                } else {
                    if (ap->argp) funnel_retarget(ap, tmpB);
                    fn->phase = 0;
                }
            }
        }
        float r = ap->sdp->paramf[0];
        ap->speed = r;
        if (r == 0.0f) ap->speed = 5.0f;
    }
    if (ap->hcol >= 0 && ap->hcol2 >= 0) DF_COLL_SetVel(ap->hcol2, ap->n_speed, ap->speed);
    if ((int)ap->spell->lifetime < ap->frame) {
        kill(ap);
        return;
    }
    spell_matrix(ap);
}

/* program 16 (funnel, Init16 / Hit16 / Kill01): coast params[0] frames (speed *= accel from start_acc), then
 * phase 2: Search every frame (the funnel stays where it coasted to); phase 1: hover near the target (< 150): face
 * it and fire sub_sid every params[1] frames; too far: retarget + phase 0 (turn only, frame NOT counted: asm-only).
 * SetPos / SetVel (hcol2) before the move with last frame's speed; funnel_id never set (sic: slot 0 for all);
 * dead ang2vec kept; the use & 4 arm kills on a failed Search and falls through to the lifetime kill (sic). */
static void __spell_MainProg16(_spell2 *ap)
{
    PS2_FRAME(0xb0);
    PS2_STACK(float[4], tmp, 0x80);
    PS2_STACK(float[4], av, 0x90);
    PS2_STACK(float[4], d, 0xa0);
    StModel *model = (StModel *)ap->argp;
    SPELL2_FUNNEL_ST *fn = &ap->dat.funnel;
    int chg = 0;
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        if (ap->hcol2 >= 0) {
            DF_COLL_SetPos(ap->hcol2, ap->pos);
            DF_COLL_SetVel(ap->hcol2, ap->n_speed, ap->speed);
        }
    }
    if (ap->frame < (int)ap->sdp->params[0]) {   /* frame before the increment */
        ap->frame++;
        add3(ap->pos, ap->rot);
        float a = ap->spell->accel;
        if (a != 0.0f && !(ap->frame < (int)ap->spell->start_acc) && ap->speed != 0.0f) {
            ap->speed = ap->speed * a;
            chg = 1;
        }
        if (chg) spell_rot(ap);
    } else if (fn->phase == 0) {
        if (__spell_MainProg15_setang(ap) > 0) fn->phase = 1;   /* frame not counted (sic) */
    } else if (fn->phase != 1) {
        ap->frame++;
        if (__spell_MainProg15_Search(ap)) fn->phase = 1;   /* overrides Search's phase 0 */
    } else {
        ap->frame++;
        if (model) {
            if (model->use & 4) {
                if (!__spell_MainProg15_Search(ap)) {
                    DF_MAGIC2_AppearMagic(0x30, ap->matrix[3], ap->ang, 0, 0, 0, 0, 0);
                    kill(ap);   /* falls through (sic) */
                }
            } else {
                vu_sub_xyz(d, ap->pos, model->loc);
                if (len3(d) < 150.0f) {
                    vu_normalize(d, d);
                    float hz = ps2_sqrt(d[0] * d[0] + d[2] * d[2]);
                    fn->tgt_ang[0] = ps2_atan2f(-d[1], hz);
                    fn->tgt_ang[1] = ps2_atan2f(d[0], d[2]);
                    fn->tgt_ang[1] = inverse_angf(fn->tgt_ang[1]);
                    __spell_MainProg15_setang(ap);
                    if (!ps2_mod32(ap->frame, ap->sdp->params[1])) {
                        ang2vec(av, wrap_pi(k_pi + ap->ang[1]), ap->ang[0]);   /* result unused (sic) */
                        _stSpell2Data *sdp = ap->sdp;
                        if (ap->comb)
                            DF_SPELL2_AppearSpellPtr((_stSpell2Data *)(uintptr_t)(ap->comb->spell_ofs + sdp->sub_sid * 0xc0),
                                                     (char)ap->flag.kind, (char)ap->flag.id, ap->dstat, 0, ap->comb,
                                                     ap->pos, ap->ang, 0, 0, 0, ap->flags, ap->weapon_id);
                        else if (sdp->sub_sid)
                            DF_SPELL2_AppearSpellPtr(SpellData2 + sdp->sub_sid, (char)ap->flag.kind, (char)ap->flag.id,
                                                     ap->dstat, 0, 0, ap->pos, ap->ang, 0, 0, 0, ap->flags,
                                                     ap->weapon_id);
                    }
                } else {
                    if (ap->argp) funnel_retarget(ap, tmp);
                    fn->phase = 0;
                }
            }
        }
    }
    if ((int)ap->spell->lifetime < ap->frame) {
        DF_MAGIC2_AppearMagic(0x30, ap->matrix[3], ap->ang, 0, 0, 0, 0, 0);
        kill(ap);
        return;
    }
    spell_matrix(ap);
}

/* Funnel_Init, argp dropped (the funnel finds its own target), start in the search phase */
static void __spell_InitProg16(_spell2 *ap)
{
    PS2_FRAME(0x20);
    DF_SPELL2_Funnel_Init(ap);
    ap->argp = 0;
    ap->dat.funnel.phase = 2;
}

/* rot = tracef * (trg - pos), speed = |rot|, n_speed = rot / speed (w 1), collision entry 2 */
static float __spell_MainProg15_set_speed(_spell2 *ap, float *trg, float tracef)
{
    PS2_FRAME(0x20);
    ap->rot[0] = tracef * (trg[0] - ap->pos[0]);
    ap->rot[1] = tracef * (trg[1] - ap->pos[1]);
    ap->rot[2] = tracef * (trg[2] - ap->pos[2]);
    float len = len3(ap->rot);   /* mula / madd */
    ap->speed = len;
    if (!(len <= 0.0f)) {
        float inv = ps2_div(1.0f, len);
        ap->n_speed[0] = ap->rot[0] * inv;
        ap->n_speed[1] = ap->rot[1] * inv;
        ap->n_speed[2] = ap->rot[2] * inv;
    } else {
        ap->n_speed[0] = 0.0f; ap->n_speed[1] = 0.0f; ap->n_speed[2] = 0.0f;
    }
    ap->n_speed[3] = 1.0f;
    if (ap->hcol2 >= 0) {
        DF_COLL_SetPos(ap->hcol2, ap->pos);
        DF_COLL_SetVel(ap->hcol2, ap->n_speed, ap->speed);
    }
    return ap->speed;
}

/* Funnel_Init, the next funnel slot id (0..5), slot retarget when a target is given, then the first velocity
 * towards the slot (tracef = spell->speed clamped to [0, 1]) */
static void __spell_InitProg15(_spell2 *ap)
{
    PS2_FRAME(0x60);
    PS2_STACK(float[4], tmp, 0x50);
    DF_SPELL2_Funnel_Init(ap);
    ap->dat.funnel.funnel_id = s_FunnelID;
    s_FunnelID = (unsigned char)(s_FunnelID + 1);
    if (!(s_FunnelID < 6)) s_FunnelID = 0;
    if (ap->argp) funnel_retarget(ap, tmp);
    float tracef = ap->spell->speed;
    if (tracef < 0.0f || 1.0f < tracef) tracef = 1.0f;
    __spell_MainProg15_set_speed(ap, ap->dat.funnel.tgt_pos, tracef);
}

/* pose from the ObjCMat (full matrix product + MatrixZtoAng into a stack temp, added to the spawner's ang), launch
 * tail (rand #1-#3), SPL2 magic with n_speed, sub magic sub_mid[1] unless s_MazzleFlag, voice, two collision entries
 * (entry 1 flag 0x60, sphere paramf[0] or 5; the first SetSelf is (hcol, 5, hcol), sic) */
static void DF_SPELL2_Funnel_Init(_spell2 *ap)
{
    PS2_FRAME(0x90);
    PS2_STACK(float[4], ang, 0x40);
    ang[0] = 0.0f; ang[1] = 0.0f; ang[2] = 0.0f; ang[3] = 1.0f;   /* copy of main 0x2606a0 */
    ObjCMatSt *oc = ap->ocmp;
    if (oc) {
        if (oc->parentmatp) {
            PS2_STACK(float[4][4], M, 0x50);
            vu_mul_matrix(M, *oc->parentmatp, oc->mat);
            copy4(ap->pos, M[3]);
            MatrixZtoAng(ang, M);
        } else {
            copy4(ap->pos, oc->mat[3]);
            MatrixZtoAng(ang, oc);
        }
    }
    add3(ap->ang, ang);
    spell_launch_tail(ap, ap->spell->speed);   /* rand #1-#3 */
    ap->id = (unsigned short)magic_spl2(ap, ap->sdp->mid, 0, 0, &ap->matrix, &ap->n_speed, 4);
    if (!s_MazzleFlag && ap->sdp->sub_mid[1] > 0) {
        if (ap->ocmp && ap->ocmp->parentmatp) magic_ocmp(ap, ap->sdp->sub_mid[1], 4);
        else magic_at(ap, ap->sdp->sub_mid[1], ap->matrix[3], ap->ang, 0);
    }
    __spell_CallPointVoice(ap, 1);
    struct_88b6ff44 *sp = ap->spell;
    if (sp->se_id) ap->snd_id = (char)Voice_CallTraceLoc(sp->se_id, sp->se_vol, &ap->matrix[3], sp->se_dist);
    ap->hcol = (short)DF_COLL_AddList(ap, s_SpellFunc[ap->sdp->pid].hitfunc, 1);
    if (ap->hcol < 0) return;
    DF_COLL_SetPos(ap->hcol, ap->pos);
    DF_COLL_SetFlag(ap->hcol, 0x60, 1);
    float r = ap->sdp->paramf[0];
    if (r <= 0.0f) r = 5.0f;
    DF_COLL_SetSphr(ap->hcol, r);
    ap->flag.sphere = 1;
    ap->hcol2 = (short)DF_COLL_AddList(ap, s_SpellFunc[ap->sdp->pid].hitfunc, 1);
    if (ap->hcol2 < 0) return;
    DF_COLL_SetSelf(ap->hcol, 5, ap->hcol);   /* sic: Init17 passes hcol2 */
    DF_COLL_SetSelf(ap->hcol2, 5, ap->hcol);
    DF_COLL_SetFlag(ap->hcol2, 0x10, 1);
    DF_COLL_SetPos(ap->hcol2, ap->pos);
    DF_COLL_SetVel(ap->hcol2, ap->n_speed, ap->speed);
}

/* ---------------------------------------------------------------- prog 11: parabolic shot (also pid 24's main) */

/* a child of the expiring shot at its pos / ang: argp = the shot itself, flag = flags | 0x80, result unused */
static void prog11_spawn(_spell2 *ap, _stSpell2Data *sdp)
{
    DF_SPELL2_AppearSpellPtr(sdp, (char)ap->flag.kind, (char)ap->flag.id, ap->dstat, 0, ap->comb, ap->pos, ap->ang, 0,
                             0, ap, (unsigned short)(ap->flags | 0x80), ap->weapon_id);
}

/* flight: collision pos / vel (vel = |rot| incl. gravity), pos += rot, xz speed *= accel, gravity on rot.y, fall-time
 * and range limits force frame = lifetime (expiry on the next call); rot.xz rebuilt from the initial n_speed and the
 * angle re-derived from it every frame (sic). Expiry: params[0] == 1 -> sub_sid along the current rot (vu normalise),
 * else hit_sid[0] (comb arm without a zero test, sic); then the kill function. */
static void __spell_MainProg11(_spell2 *ap)
{
    PS2_FRAME(0xa0);
    PS2_STACK(float[4], n, 0x90);
    ap->frame++;
    if (!(ap->frame < (int)ap->spell->lifetime)) {
        _stSpell2Data *sdp = ap->sdp;
        if (sdp->params[0] == 1) {
            if (sdp->sub_sid > 0) {
                PS2_STACK(float[4], a, 0x80);
                vu_normalize(n, ap->rot);   /* w = 0 */
                dir2ang(a, n);
                copy4(ap->ang, a);
                if (ap->comb) prog11_spawn(ap, (_stSpell2Data *)(uintptr_t)(ap->comb->spell_ofs + sdp->sub_sid * 0xc0));
                else prog11_spawn(ap, SpellData2 + sdp->sub_sid);
            }
        } else if (ap->comb) {
            prog11_spawn(ap, (_stSpell2Data *)(uintptr_t)(ap->comb->spell_ofs + sdp->hit_sid[0] * 0xc0));
        } else if (sdp->hit_sid[0]) {
            prog11_spawn(ap, SpellData2 + sdp->hit_sid[0]);
        }
        kill(ap);
        return;
    }
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        float len = len3(ap->rot);
        n[0] = ps2_div(ap->rot[0], len);
        n[1] = ps2_div(ap->rot[1], len);
        n[2] = ps2_div(ap->rot[2], len);
        n[3] = 1.0f;
        DF_COLL_SetVel(ap->hcol, n, len);
        if (ap->hcol2 >= 0) {
            DF_COLL_SetPos(ap->hcol2, ap->pos);
            DF_COLL_SetVel(ap->hcol2, n, len);
        }
    }
    add3(ap->pos, ap->rot);
    SPELL2_PARABOLIC_ST *pb = &ap->dat.parabolic;
    float acc = ap->spell->accel;
    if (!(acc == 0.0f)) pb->xz_speed = pb->xz_speed * acc;
    ap->rot[1] = ap->rot[1] + ps2_u2f(0x3d2c0831);
    if (ap->sdp->params[2] > 0 && !(ap->rot[1] <= 0.0f)) {
        pb->fall_time++;
        if (ap->sdp->params[2] < pb->fall_time) ap->frame = ap->spell->lifetime;
    }
    ap->rot[0] = ap->n_speed[0] * pb->xz_speed;
    ap->rot[2] = ap->n_speed[2] * pb->xz_speed;
    ap->rot[3] = 1.0f;
    PS2_STACK(float[4], a, 0x70);
    dir2ang(a, ap->n_speed);
    copy4(ap->ang, a);
    spell_matrix(ap);
    pb->dist = pb->dist + len3(ap->rot);
    if (!(pb->dist < ap->spell->range)) ap->frame = ap->spell->lifetime;
}

/* InitProg17's pose + launch with the base speed |argp->rot| (argp read as a _spell2, sic) or spell->speed, the SPL2
 * magic, sub magic sub_mid[1] unless s_MazzleFlag, voice, one collision entry with the table's hit function */
static void __spell_InitProg11_sub(_spell2 *ap)
{
    PS2_FRAME(0xb0);
    float par_speed;
    if (ap->argp) par_speed = len3(((_spell2 *)ap->argp)->rot);
    else par_speed = ap->spell->speed;
    spell_pose_launch_base(ap, par_speed);   /* rand #1-#3 */
    ap->id = (unsigned short)magic_spl2(ap, ap->sdp->mid, 0, 0, &ap->matrix, 0, 4);
    if (!s_MazzleFlag && ap->sdp->sub_mid[1] > 0) {
        if (ap->ocmp && ap->ocmp->parentmatp) magic_ocmp(ap, ap->sdp->sub_mid[1], 4);
        else magic_at(ap, ap->sdp->sub_mid[1], ap->matrix[3], ap->ang, 0);
    }
    __spell_CallPointVoice(ap, 1);
    struct_88b6ff44 *sp = ap->spell;
    if (sp->se_id) ap->snd_id = (char)Voice_CallTraceLoc(sp->se_id, sp->se_vol, &ap->matrix[3], sp->se_dist);
    ap->hcol = (short)DF_COLL_AddList(ap, s_SpellFunc[ap->sdp->pid].hitfunc, 1);
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        DF_COLL_SetVel(ap->hcol, ap->n_speed, ap->speed);
        __spell_SetSelfNum(ap, 0);
    }
}

/* with flags & 0x80 and an argp, the sub runs on a stack copy of the spell record; the restore tests argp only, so
 * argp != NULL without bit 7 leaves ap->spell = NULL (original bug, kept) */
static void __spell_InitProg11(_spell2 *ap)
{
    PS2_FRAME(0xa0);
    PS2_STACK(struct_88b6ff44, splact, 0x50);
    void *par = ap->argp;
    struct_88b6ff44 *bak = 0;
    if ((ap->flags & 0x80) && par) {
        CopyMemory32b(&splact, ap->spell, 0x14);
        bak = ap->spell;
        ap->spell = &splact;
    }
    __spell_InitProg11_sub(ap);
    if (par) ap->spell = bak;
    ap->dat.parabolic.xz_speed = ps2_sqrt(ap->rot[0] * ap->rot[0] + ap->rot[2] * ap->rot[2]);   /* mula / madd */
}

/* ---------------------------------------------------------------- prog 10: missile salvo carrier */

/* one missile of the salvo: random direction around the flight heading, nearest target in a 30-degree cone,
 * sub_sid spawned with it as argp; the last one (frame >= lifetime + (n - 1) * 10) kills the carrier */
static void __spell_MainProg10_missile_fire(_spell2 *ap)
{
    PS2_FRAME(0x90);
    PS2_STACK(float[4], ang, 0x70);   /* handed to DF_get_nearest (stack residue kept) */
    float a[4];
    dir2ang(a, ap->n_speed);
    copy4(ang, a);
    angrand(ang, ang, k_pi2, k_pi);
    ang[0] = wrap_pi(ang[0]);
    ang[1] = wrap_pi(ang[1]);
    ang[2] = wrap_pi(ang[2]);
    int kind = ap->flag.kind, id = ap->flag.id;
    StModel *m = Sfunc_GetShooterModel(kind, id);
    unsigned int mask = 0;
    if (m) {
        mask = m->cflag & 7;
        if (ap->flag.kind == 2 && !(((AcPlayerSt *)m->arg0p)->param.head_addon & 4)) mask = (mask | 0x200) & 0xffff;
        mask = (mask | 0x3000) & 0xffff;
    }
    float range = ps2_u2f(0x43960000);   /* 300 */
    if (!(ap->sdp->paramf[0] <= 0.0f)) range = ap->sdp->paramf[0];
    const float k30 = ps2_u2f(0x3f060a92);
    StModel *tgt = DF_get_nearest(ap->pos, ang, range, k30, k30, (unsigned short)mask, ap->flag.kind, ap->flag.id);
    _spell2 *sap;
    if (ap->comb) {
        sap = DF_SPELL2_AppearSpellPtr((_stSpell2Data *)(uintptr_t)(ap->comb->spell_ofs + ap->sdp->sub_sid * 0xc0),
                                       (char)ap->flag.kind, (char)ap->flag.id, ap->dstat, 0, ap->comb, ap->matrix[3],
                                       ang, 0, 0, tgt, ap->flags, ap->weapon_id);
    } else {
        short s = ap->sdp->sub_sid;
        sap = s ? DF_SPELL2_AppearSpellPtr(SpellData2 + s, (char)ap->flag.kind, (char)ap->flag.id, ap->dstat, 0, 0,
                                           ap->matrix[3], ang, 0, 0, tgt, ap->flags, ap->weapon_id)
                : 0;
    }
    if (sap) sap->frame = sap->spell->start_homing;
    int n = ap->sdp->params[1];
    if (n <= 0) n = 12;
    if (!(n < 13)) n = 12;
    if (!(ap->frame < ap->spell->lifetime + (n - 1) * 10)) {
        if (ap->on > 0) kill(ap);
    }
}

/* fires params[1] missiles (0 / >= 12: 12) on an enemy / AC hit (kind 3 / 4), then dies */
static void __spell_HitProg10(void *own, void *tgt, int result)
{
    PS2_FRAME(0x40);
    _spell2 *ap = (_spell2 *)own;
    if (!(ap->on > 0) || !tgt || ap->frame < ap->spell->start_hit) return;
    if (__spell_Hit(own, tgt, result, ap, 0, 0) == -1) return;
    int k = result >> 28 & 0xf;
    if (k == 3 || k == 4) {
        int n = ap->sdp->params[1];
        if (n <= 0) n = 12;
        if (!(n < 12)) n = 12;
        for (int i = 0; i < n; i++) __spell_MainProg10_missile_fire(ap);
    }
    kill(ap);
}

/* flies (decelerating by accel from start_acc) for lifetime frames, then stops and fires a missile every 10th
 * frame */
static void __spell_MainProg10(_spell2 *ap)
{
    PS2_FRAME(0x40);
    PS2_STACK(float[4], a, 0x30);
    ap->frame++;
    int use_acc = 0;
    if (ap->frame < ap->spell->lifetime) {
        if (ap->hcol >= 0) {
            DF_COLL_SetPos(ap->hcol, ap->pos);
            DF_COLL_SetVel(ap->hcol, ap->n_speed, ap->speed);
        }
        add3(ap->pos, ap->rot);
        if (!(ap->frame < ap->spell->start_acc) && !(ap->speed <= 0.0f)) {
            ap->speed = ap->speed * ap->spell->accel;
            if (ap->speed < ps2_u2f(0x38d1b717)) ap->speed = 0.0f;
            use_acc = 1;
        }
        if (use_acc) spell_rot(ap);
    } else {
        if (ap->hcol >= 0 && ap->frame == ap->spell->lifetime) {
            DF_COLL_Delete(ap->hcol);
            ap->hcol = -1;
        }
        if (ps2_mod32(ap->frame - ap->spell->lifetime, 10) == 0) __spell_MainProg10_missile_fire(ap);
    }
    dir2ang(a, ap->n_speed);
    copy4(ap->ang, a);
    spell_matrix(ap);
}

static void __spell_InitProg10(_spell2 *ap)
{
    PS2_FRAME(0x90);
    PS2_STACK(float[4][4], M, 0x50);
    PS2_STACK(float[4], d, 0x40);
    PS2_STACK(float[4], a, 0x30);
    ObjCMatSt *oc = ap->ocmp;
    if (oc && oc->parentmatp) {
        vu_mul_matrix(M, *oc->parentmatp, oc->mat);
        copy4(ap->pos, M[3]);
        float *t = ap->target;
        if (t[0] == 0.0f && t[1] == 0.0f && t[2] == 0.0f) {
            MatrixZtoAng(ap->ang, M);
        } else {
            d[0] = t[0] - ap->pos[0]; d[1] = t[1] - ap->pos[1]; d[2] = t[2] - ap->pos[2];
            d[3] = t[3];   /* vsub.xyz: w = the target's */
            dir2ang(a, d);
            copy4(ap->ang, a);
        }
    }
    spell_er(ap);
    spell_init_speed(ap);
    DF_CALC_Angle2Vector(ap->n_speed, ap->ang);
    spell_rot(ap);
    spell_matrix(ap);
    _stSpell2Data *sdp = ap->sdp;
    ap->id = (unsigned short)magic_spl2(ap, sdp->mid, 0, 0, &ap->matrix, (float (*)[4])ap->n_speed, 4);
    if (ap->sdp->sub_mid[0] > 0) {
        if (ap->ocmp && ap->ocmp->parentmatp) magic_ocmp(ap, ap->sdp->sub_mid[0], 4);
        else magic_at(ap, ap->sdp->sub_mid[0], ap->matrix[3], ap->ang, 0);
    }
    if (ap->sdp->sub_mid[1] > 0) magic_at(ap, ap->sdp->sub_mid[1], ap->matrix[3], ap->ang, 0);
    __spell_CallPointVoice(ap, 1);
    struct_88b6ff44 *sp = ap->spell;
    if (sp->se_id) ap->snd_id = (char)Voice_CallTraceLoc(sp->se_id, sp->se_vol, &ap->matrix[3], sp->se_dist);
    ap->hcol = (short)DF_COLL_AddList(ap, s_SpellFunc[ap->sdp->pid].hitfunc, 1);
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        DF_COLL_SetVel(ap->hcol, ap->n_speed, ap->speed);
        __spell_SetSelfNum(ap, 0);
    }
}

/* ---------------------------------------------------------------- prog 07: decoy flare */

/* ---------------------------------------------------------------- prog 09: swinging decoy */

/* decelerates (accel from start_acc) until frame params[0], then points straight down and flips the sign of the
 * speed every params[1] frames (dat.decoy work / decoy_time); dies at frame >= lifetime.
 * Quirk (kept): the speed is rescaled by paramf[0] every frame of the flip phase, but rot (the real movement) is
 * refreshed only when flag is set (slow-down frames, frame params[0], each flip); on frame params[0] the speed is
 * scaled twice. */
static void __spell_MainProg09(_spell2 *ap)
{
    PS2_FRAME(0x50);
    PS2_STACK(float[4], a, 0x40);
    int flag = 0;
    SPELL2_DECOY_ST *dc = &ap->dat.decoy;
    ap->frame++;
    if (!(ap->frame < ap->spell->lifetime)) {
        DF_MAGIC2_AppearMagic(0x30, ap->matrix[3], ap->ang, 0, 0, 0, 0, 0);
        kill(ap);
        return;
    }
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        if (ap->hcol2 >= 0) {
            DF_COLL_SetPos(ap->hcol2, ap->pos);
            DF_COLL_SetVel(ap->hcol2, ap->n_speed, ap->speed);
        }
    }
    add3(ap->pos, ap->rot);
    int p0 = ap->sdp->params[0];
    if (ap->frame < p0) {
        float acc = ap->spell->accel;
        if (acc != 0.0f && !(ap->frame < ap->spell->start_acc) && ap->speed != 0.0f) {
            ap->speed = ap->speed * acc;
            flag = 1;
        }
    } else {
        if (ap->frame == p0) {
            ap->n_speed[0] = 0.0f;
            ap->n_speed[1] = -1.0f;
            ap->n_speed[2] = 0.0f;
            ap->n_speed[3] = 1.0f;
            float s = ap->speed;
            if (s < 0.0f) s = -s;
            flag = 1;
            ap->speed = ap->sdp->paramf[0] * s;
            dc->work = 1;
            dc->decoy_time = 0;
        }
        if (dc->work & 1) {
            float s = ap->speed;
            if (s < 0.0f) s = -s;
            ap->speed = ap->sdp->paramf[0] * s;
            dc->decoy_time++;
            if (ap->sdp->params[1] < dc->decoy_time) {
                dc->decoy_time = 0;
                dc->work = 0;
                float t = ap->speed;
                if (t < 0.0f) t = -t;
                ap->speed = -t;
                flag = 1;
            }
        } else {
            float s = ap->speed;
            if (s < 0.0f) s = -s;
            ap->speed = ap->sdp->paramf[0] * -s;
            dc->decoy_time++;
            if (ap->sdp->params[1] < dc->decoy_time) {
                dc->decoy_time = 0;
                dc->work = 1;
                float t = ap->speed;
                if (t < 0.0f) t = -t;
                ap->speed = t;
                flag = 1;
            }
        }
    }
    if (flag) spell_rot(ap);
    dir2ang(a, ap->n_speed);
    copy4(ap->ang, a);
    spell_matrix(ap);
}

/* InitProg17's pose + launch (rand #1-#3), magic / sub magic / voice as Init17, then a decoy sphere of radius
 * paramf[1] (<= 0: 5) plus the velocity collision entry */
static void __spell_InitProg09(_spell2 *ap)
{
    PS2_FRAME(0xc0);
    spell_pose_launch(ap);
    ap->id = (unsigned short)magic_spl2(ap, ap->sdp->mid, 0, 0, &ap->matrix, 0, 4);
    if (ap->sdp->sub_mid[0] > 0) {
        if (ap->ocmp && ap->ocmp->parentmatp) magic_ocmp(ap, ap->sdp->sub_mid[0], 4);
        else magic_at(ap, ap->sdp->sub_mid[0], ap->matrix[3], ap->ang, 0);
    }
    __spell_CallPointVoice(ap, 1);
    struct_88b6ff44 *sp = ap->spell;
    if (sp->se_id) ap->snd_id = (char)Voice_CallTraceLoc(sp->se_id, sp->se_vol, &ap->matrix[3], sp->se_dist);
    ap->dat.decoy.work = 1;
    ap->hcol = (short)DF_COLL_AddList(ap, s_SpellFunc[ap->sdp->pid].hitfunc, 1);
    if (ap->hcol >= 0) {
        float r = ap->sdp->paramf[1];
        if (r <= 0.0f) r = 5.0f;
        DF_COLL_SetPos(ap->hcol, ap->pos);
        DF_COLL_SetSphr(ap->hcol, r);
        ap->flag.sphere = 1;
        ap->hcol2 = (short)DF_COLL_AddList(ap, s_SpellFunc[ap->sdp->pid].hitfunc, 1);
        if (ap->hcol2 >= 0) {
            DF_COLL_SetSelf(ap->hcol, 5, ap->hcol2);
            DF_COLL_SetSelf(ap->hcol2, 5, ap->hcol);
            DF_COLL_SetFlag(ap->hcol2, 0x10, 1);
            DF_COLL_SetPos(ap->hcol2, ap->pos);
            DF_COLL_SetVel(ap->hcol2, ap->n_speed, ap->speed);
        }
    }
}

static void __spell_KillProg07(_spell2 *ap) { __spell_KillSpellProc(ap); }

/* n_speed / speed / ang towards the decoy (argp); QUIRK: speed is left at 1 / length */
static void decoy_dir(_spell2 *ap, _spell2 *dc)
{
    ap->n_speed[0] = dc->pos[0] - ap->pos[0];
    ap->n_speed[1] = dc->pos[1] - ap->pos[1];
    ap->n_speed[2] = dc->pos[2] - ap->pos[2];
    ap->n_speed[3] = dc->pos[3];
    ap->speed = len3(ap->n_speed);
}

static void decoy_ang(_spell2 *ap)
{
    if (!(ap->speed <= 0.0f)) {
        ap->speed = ps2_div(1.0f, ap->speed);
        ap->n_speed[0] = ap->n_speed[0] * ap->speed;
        ap->n_speed[1] = ap->n_speed[1] * ap->speed;
        ap->n_speed[2] = ap->n_speed[2] * ap->speed;
        ap->n_speed[3] = 1.0f;
    }
    dir2ang(ap->ang, ap->n_speed);
    ap->ang[0] = -ap->ang[0];
    ap->ang[1] = ap->ang[1] + k_pi;
    ap->ang[0] = wrap_pi(ap->ang[0]);
    ap->ang[1] = wrap_pi(ap->ang[1]);
    ap->ang[2] = wrap_pi(ap->ang[2]);
}

static void __spell_MainProg07(_spell2 *ap)
{
    PS2_FRAME(0x80);
    ap->frame++;
    if (ap->spell->lifetime < ap->frame || (ap->dat.missile.decoy && ap->dat.missile.decoy->on <= 0)) {
        ap->dat.missile.decoy = 0;
        kill(ap);
        return;
    }
    _magic2_st *m = DF_MAGIC2_SearchID(ap->id);
    _spell2 *dc = ap->dat.missile.decoy;
    if (dc) {
        if (!m) {
            ap->frame = ap->spell->lifetime;
            return;
        }
        ObjCMatSt *oc = ap->ocmp;
        if (oc) {
            if (oc->parentmatp) vu_mul_matrix(ap->matrix, *oc->parentmatp, oc->mat);
            else sceVu0CopyMatrix(ap->matrix, oc->mat);
            copy4(ap->pos, ap->matrix[3]);
        }
        decoy_dir(ap, dc);
        if (m->mdp->param[0] != 0.0f) m->scale[2] = ps2_div(ap->speed, m->mdp->param[0]);
        decoy_ang(ap);
        if (ap->frame == 1 && rand_pct() < ap->spell->deflect) {
            int h = ap->sdp->hit_sid[0];
            if (h > 0) spell_spawn(ap, h, 0, 0, 0, ap->dat.missile.decoy->matrix[3], 0);
            kill(ap->dat.missile.decoy);
            g_nEneHitBullet++;
        }
        spell_matrix(ap);
        return;
    }
    if (!m) {
        ap->frame = ap->spell->lifetime;
        return;
    }
    float a = ap->spell->accel;
    if (!(a <= 0.0f)) {
        float mx = ap->spell->max_speed;
        if (!(mx < 0.0f) && (mx == 0.0f || ap->speed < mx)) {
            ap->speed = ap->speed + ap->spell->accel;
            mx = ap->spell->max_speed;
            if (!(mx <= 0.0f) && !(ap->speed <= mx)) ap->speed = mx;
        }
    }
    if (m->mdp->param[0] != 0.0f) m->scale[2] = ps2_div(ap->speed, m->mdp->param[0]);
}

static void __spell_InitProg07(_spell2 *ap)
{
    PS2_FRAME(0xa0);
    int have = 0;
    float M[4][4];
    ObjCMatSt *oc = ap->ocmp;
    if (oc && oc->parentmatp) {
        vu_mul_matrix(M, *oc->parentmatp, oc->mat);
        copy4(ap->pos, M[3]);
        have = 1;
    }
    ap->dat.missile.decoy = (_spell2 *)ap->argp;
    if (ap->dat.missile.decoy && ap->dat.missile.decoy->on <= 0) ap->dat.missile.decoy = 0;
    if (!ap->dat.missile.decoy) {
        if (have) MatrixZtoAng(ap->ang, M);
        DF_CALC_Angle2Vector(ap->n_speed, ap->ang);
    } else {
        decoy_dir(ap, ap->dat.missile.decoy);
        decoy_ang(ap);
    }
    oc = ap->ocmp;
    if (oc) {
        if (oc->parentmatp) vu_mul_matrix(ap->matrix, *ap->ocmp->parentmatp, ap->ocmp->mat);
        else sceVu0CopyMatrix(ap->matrix, ap->ocmp->mat);
        copy4(ap->pos, ap->matrix[3]);
        MatrixZtoAng(ap->ang, ap->matrix);
    } else {
        spell_matrix(ap);
    }
    if (ap->dat.missile.decoy) {
        decoy_dir(ap, ap->dat.missile.decoy);
        decoy_ang(ap);
        spell_matrix(ap);
    }
    ap->id = (unsigned short)magic_spl2(ap, ap->sdp->mid, 0, 0, &ap->matrix, 0, 4);
    if (ap->sdp->sub_mid[0] > 0) {
        if (ap->ocmp && ap->ocmp->parentmatp) magic_ocmp(ap, ap->sdp->sub_mid[0], 4);
        else magic_at(ap, ap->sdp->sub_mid[0], ap->matrix[3], ap->ang, 0);
    }
    if (!s_MazzleFlag && ap->sdp->sub_mid[1] > 0) magic_at(ap, ap->sdp->sub_mid[1], ap->matrix[3], ap->ang, 0);
    __spell_CallPointVoice(ap, 1);
    struct_88b6ff44 *sp = ap->spell;
    if (sp->se_id) ap->snd_id = (char)Voice_CallTraceLoc(sp->se_id, sp->se_vol, &ap->matrix[3], sp->se_dist);
}

/* ---------------------------------------------------------------- prog 04: missile */

static void kill_smoke_magics(_spell2 *ap)
{
    if (!ap->sdp->sub_mid[0]) return;
    int n = ap->sdp->params[1];
    if (!(n < 9)) n = 8;
    for (int i = 0; i < n; i++) {
        if (ap->swk[i]) {
            DF_MAGIC2_Kill(Mag2St + ap->swk[i]);
            ap->swk[i] = 0;
        }
    }
}

static void __spell_KillProg04(_spell2 *ap)
{
    PS2_FRAME(0x50);
    DF_COLL_Delete(ap->hcol);
    ap->hcol = -1;
    kill_smoke_magics(ap);
    __spell_KillSpellProc(ap);
}

/* program 05's fan angle for child i: pi * (R * (P / 2 - i)) / 180 */
static float prog05_fan(_spell2 *ap, int i)
{
    float h = ps2_div((float)ap->sdp->params[0], 2.0f);
    return ps2_div(k_pi * (ap->sdp->paramf2[2] * (h - (float)i)), 180.0f);
}

/* program 05 (Init04 / Hit02 / Kill04): a homing missile that bursts into params[0] sub_sid missiles once it is
 * within paramf[0] of its target (from frame 2) or at expiry (lifetime < frame). Burst directions: the flight
 * heading + add_ang[i] (paramf2[0] == paramf2[1] == 0: a +-R cross in pitch / yaw; paramf2[0] == 0: a pitch fan;
 * paramf2[1] == 0: a yaw fan; degrees R = paramf2[2]). The children inherit argp / flags / weapon_id and the
 * parent's speed (speed_rate[0]), roll their own alter (speed untouched) and start at frame start_homing.
 * Flight: MainProg04 without decoy / Aberrance arms, MissileHoming with decoy flag 1 (sic).
 * (sic) add_ang has 6 entries but the loops run to params[0]: entries 6 / 7 are ang / homing (one stack region). */
static void __spell_MainProg05(_spell2 *ap)
{
    PS2_FRAME(0x120);
    PS2_STACK(float[8][4], av, 0xa0);   /* add_ang[6] @0xa0, ang @0x100, homing @0x110 */
    PS2_STACK(float[4], c, 0x90);
    float (*add_ang)[4] = av;
    float *ang = av[6], *homing = av[7];
    for (int k = 0; k < 6; k++) {   /* the rodata table 0x2605d0 copied in on every call */
        add_ang[k][0] = 0.0f; add_ang[k][1] = 0.0f; add_ang[k][2] = 0.0f; add_ang[k][3] = 1.0f;
    }
    float dist = 99999.0f;   /* also with neither decoy nor argp (map said 0) */
    int chg = 0;
    float a[4];
    ap->frame++;
    _spell2 *dec = ap->dat.missile.decoy;
    if (dec) {
        dist = DF_CALC_GetDist(dec->pos, ap->pos);   /* through the distance table at 0x27b018 (entry 0) */
    } else if (ap->argp) {
        DF_GetModelCenter(c, (StModel *)ap->argp);
        dist = DF_CALC_GetDist(c, ap->pos);
    }
    if ((dist < ap->sdp->paramf[0] && ap->frame >= 2) || ap->spell->lifetime < ap->frame) {
        if (ap->sdp->paramf2[0] == 0.0f && ap->sdp->paramf2[1] == 0.0f) {
            add_ang[0][0] = ps2_div(k_pi * ap->sdp->paramf2[2], 180.0f);
            add_ang[1][0] = ps2_div(k_pi * -ap->sdp->paramf2[2], 180.0f);
            add_ang[2][1] = ps2_div(k_pi * ap->sdp->paramf2[2], 180.0f);
            add_ang[3][1] = ps2_div(k_pi * -ap->sdp->paramf2[2], 180.0f);
        } else if (ap->sdp->paramf2[0] == 0.0f) {
            for (int i = 0; i < ap->sdp->params[0]; i++) {
#ifdef ACNB_SOURCEPORT
                if (i >= 8) break;   /* the original writes on into its caller's frame */
#endif
                add_ang[i][0] = prog05_fan(ap, i);
            }
        } else if (ap->sdp->paramf2[1] == 0.0f) {
            for (int i = 0; i < ap->sdp->params[0]; i++) {
#ifdef ACNB_SOURCEPORT
                if (i >= 8) break;
#endif
                add_ang[i][1] = prog05_fan(ap, i);
            }
        }
        for (int i = 0; i < ap->sdp->params[0]; i++) {
#ifdef ACNB_SOURCEPORT
            if (i >= 8) break;
#endif
            dir2ang(a, ap->n_speed);
            copy4(ap->ang, a);
            float t0 = ap->ang[0] + add_ang[i][0], t1 = ap->ang[1] + add_ang[i][1], t2 = ap->ang[2] + add_ang[i][2];
            ang[0] = t0; ang[1] = t1; ang[2] = t2; ang[3] = ap->ang[3];   /* vadd.xyz: w from ap->ang */
            ang[0] = wrap_pi(ang[0]);
            ang[1] = wrap_pi(ang[1]);
            ang[2] = wrap_pi(ang[2]);
            _spell2 *sap;
            if (ap->comb) {
                sap = DF_SPELL2_AppearSpellPtr(
                    (_stSpell2Data *)(uintptr_t)(ap->comb->spell_ofs + ap->sdp->sub_sid * 0xc0), (char)ap->flag.kind,
                    (char)ap->flag.id, ap->dstat, 0, ap->comb, ap->matrix[3], ang, 0, 0, ap->argp, ap->flags,
                    ap->weapon_id);
            } else if (!ap->sdp->sub_sid) {
                sap = 0;
            } else {
                sap = DF_SPELL2_AppearSpellPtr(SpellData2 + ap->sdp->sub_sid, (char)ap->flag.kind, (char)ap->flag.id,
                                               ap->dstat, 0, 0, ap->matrix[3], ang, 0, 0, ap->argp, ap->flags,
                                               ap->weapon_id);
            }
            if (sap) {
                sap->speed = __spell_MainProg05__speed_rate[0] * ap->speed;
                if (sap->spell->MinSpeed > 0) {   /* (sic) alter re-rolled, speed not recomputed */
                    float r = rnd01();
                    float v = (float)(100 - sap->spell->MinSpeed) * r;
                    sap->alter = (unsigned char)(f2u(v) + sap->spell->MinSpeed);
                } else {
                    sap->alter = 100;
                }
                sap->frame = sap->spell->start_homing;
            }
        }
        kill(ap);
        return;
    }
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        DF_COLL_SetVel(ap->hcol, ap->n_speed, ap->speed);
    }
    add3(ap->pos, ap->rot);
    if (ap->argp && ap->spell->start_homing < ap->frame) {
        if (((StModel *)ap->argp)->use & 4) {
            ap->argp = 0;
        } else {
            __spell_MissileHoming(homing, ap, 1);   /* (sic) decoy flag 1 */
            spell_steer(ap, homing);
            chg = 1;
        }
    }
    if (spell_accel_alter(ap)) chg = 1;
    if (chg) spell_rot(ap);
    dir2ang(a, ap->n_speed);
    copy4(ap->ang, a);
    spell_matrix(ap);
    spell_trail_magic(ap);
    __spell_SmokeProc(ap);
    copy4(ap->dat.missile.pos1, ap->pos);
}

static void __spell_MainProg04(_spell2 *ap)
{
    PS2_FRAME(0x90);
    int chg = 0;
    float tdist = 0.0f;
    ap->frame++;
    if (ap->spell->lifetime < ap->frame) {
        DF_MAGIC2_AppearMagic(0x30, ap->matrix[3], ap->ang, 0, 0, 0, 0, 0);
        kill(ap);
        return;
    }
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        DF_COLL_SetVel(ap->hcol, ap->n_speed, ap->speed);
    }
    add3(ap->pos, ap->rot);
    float loc[4];
    if (!ap->dat.missile.decoy && ap->argp) {
        if (((StModel *)ap->argp)->use & 4) {
            ap->argp = 0;
        } else if (ap->spell->start_homing < ap->frame) {
            __spell_MissileHoming(loc, ap, ap->dat.missile.decoy_nochk);
            spell_steer(ap, loc);
            chg = 1;
            float c[4];
            DF_GetModelCenter(c, (StModel *)ap->argp);
            c[0] = c[0] - ap->pos[0]; c[1] = c[1] - ap->pos[1]; c[2] = c[2] - ap->pos[2];
            tdist = len3(c);
        }
    } else if (!ap->dat.missile.decoy) {
        __spell_Aberrance(ap, 8.0f, 0x28, 10, 0xb4, 0x78, 0);
        chg = 1;
    } else if (ap->dat.missile.decoy->on <= 0) {
        ap->dat.missile.decoy = 0;
        ap->argp = 0;
    } else if (ap->spell->start_homing < ap->frame) {
        __spell_MissileHomingDecoy(loc, ap);
        spell_steer(ap, loc);
        chg = 1;
        _spell2 *dc = ap->dat.missile.decoy;
        float v[3] = {dc->pos[0] - ap->pos[0], dc->pos[1] - ap->pos[1], dc->pos[2] - ap->pos[2]};
        tdist = len3(v);
        if (tdist <= 5.0f) {
            DF_MAGIC2_AppearMagic(0x30, ap->matrix[3], ap->ang, 0, 0, 0, 0, 0);
            kill(ap);
            kill(ap->dat.missile.decoy);
            g_nEneHitBullet++;
            return;
        }
    }
    int p0 = ap->sdp->params[0];
    if (0 < p0 && p0 < 9 && !(chg && tdist < 30.0f)) {
        float f = ps2_div(100.0f * (float)ap->frame, (float)ap->spell->lifetime);
        int v = ps2_ftoi(ps2_div(120.0f * (100.0f - f), 100.0f));
        if (v >= 11 && ap->frame >= 0x15) __spell_Aberrance(ap, (float)ap->sdp->params[0], v, 10, 0xb4, 0x78, 0);
    }
    if (spell_accel_alter(ap)) chg = 1;
    if (chg) spell_rot(ap);
    dir2ang(ap->ang, ap->n_speed);
    spell_matrix(ap);
    spell_trail_magic(ap);
    __spell_SmokeProc(ap);
    copy4(ap->dat.missile.pos1, ap->pos);
}

/* program 28 (missile): MainProg04's flight with a one-shot target acquisition at frame start_homing + 1 (nearest
 * lockable within paramf[0] or 300, +-30 deg; argp overwritten even with NULL; flags & 1 = lock a spell = decoy).
 * Differences to Prog04 / 26 (sic, kept): no Aberrance, no params[0] block, no trail magic, MissileHoming decoy
 * flag 0, no loc.y lowering, no g_nEneHitBullet++ on a decoy kill; the target distance is computed and dropped. */
static void __spell_MainProg28(_spell2 *ap)
{
    PS2_FRAME(0x90);
    PS2_STACK(float[4], a, 0x50);
    PS2_STACK(float[4], loc, 0x60);
    PS2_STACK(float[4], ang, 0x70);
    PS2_STACK(float[4], c, 0x80);
    int chg = 0;
    ap->frame++;
    if (ap->spell->lifetime < ap->frame) {
        DF_MAGIC2_AppearMagic(0x30, ap->matrix[3], ap->ang, 0, 0, 0, 0, 0);
        kill(ap);
        return;
    }
    if (ap->frame == (int)ap->spell->start_homing + 1) {
        float range = 300.0f;
        copy4(ang, ap->ang);
        unsigned short mask = DF_SPELL_GetShooterUlockMask(ap->flag.kind, ap->flag.id);
        if (!(ap->sdp->paramf[0] <= 0.0f)) range = ap->sdp->paramf[0];
        const float k30 = ps2_u2f(0x3f060a92);
        ap->argp = DF_get_nearest(ap->pos, ang, range, k30, k30, mask, ap->flag.kind, ap->flag.id);
        if (ap->argp) {
            if (ap->flags & 1) {
                copy4(ap->target, ((_spell2 *)ap->argp)->pos);
                ap->dat.missile.decoy = (_spell2 *)ap->argp;
            } else {
                DF_GetModelCenter(ap->target, (StModel *)ap->argp);
                ap->dat.missile.decoy = 0;
            }
        }
    }
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        DF_COLL_SetVel(ap->hcol, ap->n_speed, ap->speed);
    }
    add3(ap->pos, ap->rot);
    if (ap->spell->start_homing < ap->frame) {
        if (!ap->dat.missile.decoy && ap->argp) {
            if (((StModel *)ap->argp)->use & 4) {
                ap->argp = 0;
            } else {
                __spell_MissileHoming(loc, ap, 0);   /* (sic) decoy flag 0 */
                spell_steer(ap, loc);
                chg = 1;
                DF_GetModelCenter(c, (StModel *)ap->argp);
                c[0] = c[0] - ap->pos[0]; c[1] = c[1] - ap->pos[1]; c[2] = c[2] - ap->pos[2];   /* c.w kept */
                /* the asm takes len3(c) here and drops it */
            }
        } else if (ap->dat.missile.decoy) {
            if (ap->dat.missile.decoy->on <= 0) {
                ap->argp = 0;
                ap->dat.missile.decoy = 0;
            } else {
                __spell_MissileHomingDecoy(loc, ap);
                spell_steer(ap, loc);
                chg = 1;
                vu_sub_xyz(c, ap->dat.missile.decoy->pos, ap->pos);
                if (len3(c) <= 5.0f) {
                    DF_MAGIC2_AppearMagic(0x30, ap->matrix[3], ap->ang, 0, 0, 0, 0, 0);
                    kill(ap);
                    kill(ap->dat.missile.decoy);   /* (sic) no g_nEneHitBullet++ */
                    return;
                }
            }
        }
    }
    if (spell_accel_alter(ap)) chg = 1;
    if (chg) spell_rot(ap);
    dir2ang(a, ap->n_speed);
    copy4(ap->ang, a);
    spell_matrix(ap);
    __spell_SmokeProc(ap);
    copy4(ap->dat.missile.pos1, ap->pos);
}

/* program 26's follow-up spells: sid of ap's file (comb: no zero test), argp = the missile itself, flag = ap->flags
 * (not spell_spawn: that one passes argp / flag 0) */
static void prog26_spawn(_spell2 *ap, int sid)
{
    _stSpell2Data *sdp;
    if (ap->comb) {
        sdp = (_stSpell2Data *)(uintptr_t)(ap->comb->spell_ofs + sid * 0xc0);
    } else {
        if (!(short)sid) return;
        sdp = SpellData2 + (short)sid;
    }
    DF_SPELL2_AppearSpellPtr(sdp, (char)ap->flag.kind, (char)ap->flag.id, ap->dstat, 0, ap->comb, ap->pos, ap->ang, 0,
                             0, ap, ap->flags, ap->weapon_id);
}

/* program 26 (Init04 / Hit02 / Kill04): MainProg04's homing flight with a proximity burst - once the horizontal
 * (x, z) distance to the last homing target is under 10, the missile spawns params[0] sub_sid spells pointing
 * straight up and dies; at expiry (frame >= lifetime) one hit_sid[0] spell. Differences to Prog04 (sic, kept):
 * plain accel clamp to max_speed (no alter%), MissileHoming always gets decoy flag 0, loc.y -= paramf[0] before the
 * steer, no g_nEneHitBullet++ on a decoy kill, expiry guarded by sub_sid but spawning hit_sid[0]. */
static void __spell_MainProg26(_spell2 *ap)
{
    PS2_FRAME(0xe0);
    PS2_STACK(float[4], c, 0xc0);   /* (sic) read on every path, written only by the two homing arms: stale otherwise */
    static const float up_v[4] = {0.0f, 1.0f, 0.0f, 1.0f};   /* rodata 0x260710 / 0x260720 */
    int chg = 0;
    float a[4], loc[4];
    ap->frame++;
    if (!(ap->frame < ap->spell->lifetime)) {
        if (ap->sdp->sub_sid > 0) {
            dir2ang(a, up_v);
            copy4(ap->ang, a);
            prog26_spawn(ap, ap->sdp->hit_sid[0]);   /* (sic) guard is sub_sid */
        }
        kill(ap);
        return;
    }
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        DF_COLL_SetVel(ap->hcol, ap->n_speed, ap->speed);
    }
    add3(ap->pos, ap->rot);
    if (!ap->dat.missile.decoy && ap->argp) {
        if (((StModel *)ap->argp)->use & 4) {
            ap->argp = 0;
        } else if (ap->spell->start_homing < ap->frame) {
            __spell_MissileHoming(loc, ap, 0);   /* (sic) decoy flag 0 */
            loc[1] = loc[1] - ap->sdp->paramf[0];
            spell_steer(ap, loc);
            chg = 1;
            DF_GetModelCenter(c, (StModel *)ap->argp);
            c[0] = c[0] - ap->pos[0]; c[1] = c[1] - ap->pos[1]; c[2] = c[2] - ap->pos[2];   /* c.w kept */
            /* the asm also takes len3(c) here and drops it */
        }
    } else if (!ap->dat.missile.decoy) {
        __spell_Aberrance(ap, 8.0f, 0x28, 10, 0xb4, 0x78, 0);
        chg = 1;
    } else if (ap->dat.missile.decoy->on <= 0) {
        ap->dat.missile.decoy = 0;
        ap->argp = 0;
    } else if (ap->spell->start_homing < ap->frame) {
        __spell_MissileHomingDecoy(loc, ap);
        loc[1] = loc[1] - ap->sdp->paramf[0];
        spell_steer(ap, loc);
        chg = 1;
        _spell2 *dc = ap->dat.missile.decoy;
        c[0] = dc->pos[0] - ap->pos[0]; c[1] = dc->pos[1] - ap->pos[1]; c[2] = dc->pos[2] - ap->pos[2];
        if (len3(c) <= 5.0f) {
            DF_MAGIC2_AppearMagic(0x30, ap->matrix[3], ap->ang, 0, 0, 0, 0, 0);
            kill(ap);
            kill(ap->dat.missile.decoy);   /* (sic) no g_nEneHitBullet++ */
            return;
        }
    }
    if (ps2_sqrt(c[0] * c[0] + c[2] * c[2]) < 10.0f) {
        if (ap->sdp->sub_sid > 0) {
            int mid = ap->sdp->sub_mid[1];   /* (sic) no > 0 test */
            if (ap->comb) DF_MAGIC2_AppearMagicPtr(comb_mdp(ap, mid), ap->comb, ap->matrix[3], ap->ang, 0, 0, 0, 0, 8);
            else DF_MAGIC2_AppearMagic((unsigned short)mid, ap->matrix[3], ap->ang, 0, 0, 0, 0, 8);   /* lhu */
            for (int i = 0; i < ap->sdp->params[0]; i++) {
                dir2ang(a, up_v);
                copy4(ap->ang, a);
                ap->ang[0] = wrap_pi(ap->ang[0]);
                ap->ang[1] = wrap_pi(ap->ang[1]);
                prog26_spawn(ap, ap->sdp->sub_sid);
            }
        }
        kill(ap);
        return;
    }
    float acc = ap->spell->accel;
    if (!(acc <= 0.0f)) {
        if (!(ap->frame < ap->spell->start_acc) && ap->speed != ap->spell->max_speed) {
            ap->speed = ap->speed + acc;
            float mx = ap->spell->max_speed;
            if (!(mx <= 0.0f) && !(ap->speed <= mx)) ap->speed = mx;
            chg = 1;
        }
    } else if (acc < 0.0f) {
        if (!(ap->frame < ap->spell->start_acc) && !(ap->speed <= 0.0f)) {
            ap->speed = ap->speed + acc;
            if (ap->speed <= 0.0f) ap->speed = 0.0f;
            chg = 1;
        }
    }
    if (chg) spell_rot(ap);
    dir2ang(a, ap->n_speed);
    copy4(ap->ang, a);
    spell_matrix(ap);
    __spell_SmokeProc(ap);
    copy4(ap->dat.missile.pos1, ap->pos);
}

static void __spell_InitProg04(_spell2 *ap)
{
    PS2_FRAME(0xb0);
    float la[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    ObjCMatSt *oc = ap->ocmp;
    if (oc && oc->parentmatp) {
        float M[4][4];
        vu_mul_matrix(M, *oc->parentmatp, oc->mat);
        copy4(ap->pos, M[3]);
        MatrixZtoAng(la, M);
    }
    if (ap->target[3] == 1.0f) {
        float d[3] = {ap->target[0] - ap->pos[0], ap->target[1] - ap->pos[1], ap->target[2] - ap->pos[2]};
        dir2ang(la, d);
    }
    ap->ang[0] = ap->ang[0] + la[0]; ap->ang[1] = ap->ang[1] + la[1]; ap->ang[2] = ap->ang[2] + la[2];
    spell_er(ap);
    spell_init_speed(ap);
    DF_CALC_Angle2Vector(ap->n_speed, ap->ang);
    spell_rot(ap);
    spell_matrix(ap);
    ap->id = (unsigned short)magic_spl2(ap, ap->sdp->mid, 0, 0, &ap->matrix, (float (*)[4])ap->n_speed, 4);
    __spell_CallPointVoice(ap, 1);
    struct_88b6ff44 *sp = ap->spell;
    if (sp->se_id) ap->snd_id = (char)Voice_CallTraceLoc(sp->se_id, sp->se_vol, &ap->matrix[3], sp->se_dist);
    if (s_ThunderFlag == 1) ap->dat.missile.smoke_id = DF_MAGIC3_Appear((float (*)[4])ap->pos, 0, 0, 1);
    else if (ap->sdp->pid == 0x1e)
        ap->dat.missile.smoke_id = DF_MAGIC3_Appear((float (*)[4])ap->pos, 0, (float (*)[4])ap->rot, 2);
    else ap->dat.missile.smoke_id = DF_MAGIC3_Appear((float (*)[4])ap->pos, 0, (float (*)[4])ap->rot, 0);
    if (ap->argp) {
        if (ap->flags & 1) {
            copy4(ap->target, ((_spell2 *)ap->argp)->pos);
            ap->dat.missile.decoy = (_spell2 *)ap->argp;
        } else {
            DF_GetModelCenter(ap->target, (StModel *)ap->argp);
        }
    }
    if (!(rnd01() <= 0.75f)) ap->dat.missile.decoy_nochk = 1;
    ap->hcol = (short)DF_COLL_AddList(ap, s_SpellFunc[ap->sdp->pid].hitfunc, 1);
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        DF_COLL_SetVel(ap->hcol, ap->n_speed, ap->speed);
        __spell_SetSelfNum(ap, 0);
    }
    __spell_SmokeInit(ap);
    ap->dat.missile.stray[0] = rnd_sym();
    ap->dat.missile.stray[1] = rnd_sym();
    ap->stat.is_missile = 1;
}

/* ---------------------------------------------------------------- prog 02 / 03: bullets */

static void __spell_KillProg03(_spell2 *ap)
{
    PS2_FRAME(0x40);
    _magic2_st *m = DF_MAGIC2_SearchID(ap->id);
    if (m) m->oya_speed = ap->speed;
    DF_COLL_Delete(ap->hcol);
    ap->hcol = -1;
    __spell_KillSpellProc(ap);
    kill_smoke_magics(ap);
}

static void __spell_InitProg02_and_03(_spell2 *ap, int inflag)
{
    PS2_FRAME(0xc0);
    float la[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    ObjCMatSt *oc = ap->ocmp;
    if (oc && oc->parentmatp) {
        float M[4][4];
        vu_mul_matrix(M, *oc->parentmatp, oc->mat);
        copy4(ap->pos, M[3]);
        if (!(ap->target[3] == 1.0f)) MatrixZtoAng(la, M);
    }
    if (ap->target[3] == 1.0f) {
        float d[3] = {ap->target[0] - ap->pos[0], ap->target[1] - ap->pos[1], ap->target[2] - ap->pos[2]};
        dir2ang(la, d);
    }
    ap->ang[0] = ap->ang[0] + la[0]; ap->ang[1] = ap->ang[1] + la[1]; ap->ang[2] = ap->ang[2] + la[2];
    spell_er(ap);
    float r = rnd_sym();
    ap->speed = ap->spell->speed + ap->spell->trace * r;
    DF_CALC_Angle2Vector(ap->n_speed, ap->ang);
    spell_rot(ap);
    spell_matrix(ap);
    ap->id = (unsigned short)magic_spl2(ap, ap->sdp->mid, 0, 0, &ap->matrix, (float (*)[4])ap->n_speed, inflag | 4);
    if (!s_MazzleFlag && ap->sdp->sub_mid[1]) {
        if (ap->ocmp) magic_ocmp(ap, ap->sdp->sub_mid[1], 4);
        else magic_spl2(ap, ap->sdp->sub_mid[1], ap->matrix[3], ap->ang, 0, 0, 0);
    }
    __spell_CallPointVoice(ap, 1);
    struct_88b6ff44 *sp = ap->spell;
    if (sp->se_id) ap->snd_id = (char)Voice_CallTraceLoc(sp->se_id, sp->se_vol, &ap->matrix[3], sp->se_dist);
    float dist;
    __spell_CheckHitToObjects(ap, ap->spell->range, &dist, ap->spell->back_hit);
    if (dist == -1.0f) {
        ap->hcol = -1;
        DF_SPELL2_LineHit(ap);
        return;
    }
    ap->hcol = (short)DF_COLL_AddList(ap, s_SpellFunc[ap->sdp->pid].hitfunc, 1);
    if (ap->hcol >= 0) {
        DF_COLL_SetFlag(ap->hcol, 0x20, 1);
        DF_COLL_SetPos(ap->hcol, ap->pos);
        __spell_SetSelfNum(ap, 0);
        float r0 = ap->sdp->paramf[0];
        if (r0 != 0.0f) {
            DF_COLL_SetSphr(ap->hcol, r0);
            ap->flag.sphere = 1;
            ap->spell_type |= 8;
        } else {
            DF_COLL_SetVel(ap->hcol, ap->n_speed, ap->speed);
            ap->spell_type |= 4;
        }
    }
    __spell_SmokeInit(ap);
}

static void __spell_InitProg03(_spell2 *ap) { __spell_InitProg02_and_03(ap, 0x20); }

static void __spell_KillProg02(_spell2 *ap)
{
    PS2_FRAME(0x40);
    DF_COLL_Delete(ap->hcol);
    ap->hcol = -1;
    __spell_KillSpellProc(ap);
    kill_smoke_magics(ap);
}

static void __spell_MainProg02(_spell2 *ap)
{
    PS2_FRAME(0xc0);
    PS2_STACK(float[4], nx, 0xb0);   /* nx.w is never written: pos.w takes the stale stack word */
    int chg = 0;
    ap->frame++;
    ap->dat.gun.dist = ap->dat.gun.dist + ap->speed;
    if (!(ap->dat.gun.dist < ap->spell->range)) {
        if (ap->sdp->params[0]) spell_spawn(ap, ap->sdp->hit_sid[0], 0, 0, 0, ap->matrix[3], ap->ang);
        kill(ap);
        return;
    }
    if (ap->hcol >= 0) {
        DF_COLL_SetPos(ap->hcol, ap->pos);
        if (ap->sdp->paramf[0] == 0.0f) DF_COLL_SetVel(ap->hcol, ap->n_speed, ap->speed);
    }
    if (!(ap->flags & 4) && ap->maphit && ap->spell->start_hit < ap->frame) {
        /* the map hit found at spawn: stop on it once passed */
        float hp[4] = {ap->dat.gun.map_hit[0], ap->dat.gun.map_hit[1], ap->dat.gun.map_hit[2], 1.0f};
        float d0 = hp[0] - ap->pos[0], d1 = hp[1] - ap->pos[1], d2 = hp[2] - ap->pos[2];
        if ((d0 * ap->n_speed[0] + d1 * ap->n_speed[1]) + d2 * ap->n_speed[2] < 0.0f) {
            copy4(ap->pos, hp);
            DF_SPELL2_LineHit(ap);
            return;
        }
        nx[0] = ap->pos[0] + ap->rot[0];
        nx[1] = ap->pos[1] + ap->rot[1];
        nx[2] = ap->pos[2] + ap->rot[2];
        d0 = hp[0] - nx[0]; d1 = hp[1] - nx[1]; d2 = hp[2] - nx[2];
        if ((d0 * ap->n_speed[0] + d1 * ap->n_speed[1]) + d2 * ap->n_speed[2] < 0.0f) {
            float s = (ap->speed - ps2_sqrt((d0 * d0 + d1 * d1) + d2 * d2)) - ps2_u2f(0x3dcccccd);
            if (s <= 0.0f) {
                copy4(ap->pos, hp);
                DF_SPELL2_LineHit(ap);
                return;
            }
            if (ap->sdp->paramf[0] == 0.0f) DF_COLL_SetVel(ap->hcol, ap->n_speed, s);
            scale_rot(ap, ps2_u2f(0x3e4ccccd) + s);
            add3(ap->pos, ap->rot);
        } else {
            copy4(ap->pos, nx);
        }
    } else {
        add3(ap->pos, ap->rot);
    }
    float a = ap->spell->accel;
    if (!(a <= 0.0f)) {
        if (!(ap->frame < ap->spell->start_acc) && ap->speed != ap->spell->max_speed) {
            ap->speed = ap->speed + a;
            float mx = ap->spell->max_speed;
            if (!(mx <= 0.0f) && !(ap->speed <= mx)) ap->speed = mx;
            chg = 1;
        }
    } else if (a < 0.0f) {
        if (!(ap->frame < ap->spell->start_acc) && !(ap->speed <= 0.0f)) {
            ap->speed = ap->speed + a;
            if (ap->speed <= 0.0f) ap->speed = 0.0f;
            chg = 1;
        }
    }
    if (chg) spell_rot(ap);
    dir2ang(ap->ang, ap->n_speed);
    spell_matrix(ap);
    __spell_SmokeProc(ap);
    copy4(ap->dat.gun.pos1, ap->pos);
}

static void __spell_InitProg02(_spell2 *ap) { __spell_InitProg02_and_03(ap, 0); }

/* ---------------------------------------------------------------- prog 01: growing sphere */

/* map hit of a line spell: map part damage, then hit_sid[hit_attr] at the hit point, kill */
void DF_SPELL2_LineHit(_spell2 *ap)
{
    PS2_FRAME(0x70);
    int h = ap->hit_attr;
    if (!(h < 7)) h = 0;
    int up = 0;
    DF_SPELL2_MapHitProc_Param((ap->maphit & 0xf000) >> 12, (ap->maphit & 0xfff) - 1, ap->pos, ap->flag.kind,
                               ap->flag.id, ap->dstat);
    int sid = ap->sdp->hit_sid[h];
    if (sid > 0) {
        if (h == 1) up = 1;
        spell_spawn(ap, sid, 0, 0, 0, ap->pos, up ? 0 : ap->ang);
    }
    kill(ap);
}

static void __spell_KillProg01(_spell2 *ap)
{
    PS2_FRAME(0x20);
    DF_COLL_Delete(ap->hcol);
    ap->hcol = -1;
    __spell_KillSpellProc(ap);
}

static void __spell_MainProg01(_spell2 *ap)
{
    PS2_FRAME(0x10);
    if (!ap->sdp->params[0]) {
        kill(ap);
        return;
    }
    ap->frame++;
    if (ap->spell->lifetime < ap->frame) {
        kill(ap);
        return;
    }
    float g = ap->sdp->paramf[1] - ap->sdp->paramf[0];
    ap->dat.magic.r = ap->dat.magic.r + ps2_div(g, (float)ap->spell->lifetime);
    if (ap->hcol >= 0) DF_COLL_SetSphr(ap->hcol, ap->dat.magic.r);
}

static void __spell_InitProg01(_spell2 *ap)
{
    PS2_FRAME(0xa0);
    float M[4][4];
    ObjCMatSt *oc = ap->ocmp;
    if (oc) {
        if (oc->parentmatp) vu_mul_matrix(M, *oc->parentmatp, oc->mat);
        else sceVu0CopyMatrix(M, oc->mat);
    } else {
        sceVu0CopyMatrix(M, IdentityMtx);
    }
    spell_er(ap);
    spell_matrix(ap);
    if (ap->ocmp) vu_mul_matrix(ap->matrix, M, ap->matrix);
    copy4(ap->pos, ap->matrix[3]);
    _stSpell2Data *sdp = ap->sdp;
    magic_spl2(ap, sdp->mid, 0, 0, &ap->matrix, 0, 8);
    if (ap->sdp->sub_mid[0] > 0) {
        if (ap->ocmp && ap->ocmp->parentmatp) magic_ocmp(ap, ap->sdp->sub_mid[0], 4);
        else magic_spl2(ap, ap->sdp->sub_mid[0], 0, 0, &ap->matrix, 0, 8);
    }
    if (!s_MazzleFlag && ap->sdp->sub_mid[1]) magic_spl2(ap, ap->sdp->sub_mid[1], 0, 0, &ap->matrix, 0, 8);
    __spell_CallPointVoice(ap, 1);
    ap->flag.kind = 0;
    ap->flag.id = 0;
    if (ap->sdp->params[0] == 1) {
        ap->dat.magic.r = ap->sdp->paramf[0];
        ap->hcol = (short)DF_COLL_AddList(ap, s_SpellFunc[ap->sdp->pid].hitfunc, 1);
        DF_COLL_SetFlag(ap->hcol, 0x80, 0);
        if (ap->hcol >= 0) {
            DF_COLL_SetPos(ap->hcol, ap->matrix[3]);
            DF_COLL_SetSphr(ap->hcol, ap->dat.magic.r);
            __spell_SetSelfNum(ap, 0);
            ap->flag.sphere = 1;
        }
    }
}

static void __spell_KillProgxx(void) {}
static void __spell_HitProgxx(void) {}
static void __spell_MainProgxx(_spell2 *ap) { __spell_KillSpellProc(ap); }
static void __spell_InitProgxx(void) {}

/* ---------------------------------------------------------------- per frame / spawn / kill */

/* collects this frame's decoys (pid 9, params[2] == 0), then runs every live spell spawned before this frame */
void DF_SPELL2_SpellProc(void)
{
    PS2_FRAME(0x30);
    s_NowFrameDecoy = 0;
    _spell2 **dl = s_pNowFrameDecoy;
    _spell2 *p = sap0 + 1;
    for (int i = 1; i < 0x200; i++, p++) {
        if (p->on > 0 && p->sdp->pid == 9 && !p->sdp->params[2]) {
            *dl++ = p;
            s_NowFrameDecoy++;
        }
    }
    p = sap0 + 1;
    for (int i = 1; i < 0x200; i++, p++) {
        if (!p->on || !((unsigned int)p->myframe < FRAME)) continue;
        if (p->on < 0) {
            p->on = p->on == -1 ? 0 : -1;
            continue;
        }
        gcall(s_SpellFunc[p->sdp->pid].mainfunc, p);
        p->dist = p->dist + p->speed;
    }
}

/* the spell's second voice (se_id2); weapon shots (mode 1, or a non-AC shooter) share 4 slots that limit each
 * voice to one call per 4 frames (se_id 0x33: 20) */
static void __spell_CallPointVoice(_spell2 *ap, int mode)
{
    PS2_FRAME(0x30);
    if (ap->flags & 8) return;
    struct_88b6ff44 *sp = ap->spell;
    if (!sp->se_id2) return;
    if (ap->flag.kind != 2) mode = 1;
    if (!mode) {
        Voice_CallPoint(sp->se_id2, sp->se_vol2, &ap->matrix[3], sp->se_dist2);
        return;
    }
    if (Voice_IsFullCall()) return;
    sp = ap->spell;
    int found = -1, mi = 0;
    unsigned int mf = 0x7fffffff;
    for (int i = 0; i < 4; i++) {
        if (sp->se_id2 == s_seexp[i].id) found = i;
        if (s_seexp[i].frame < mf) {
            mf = s_seexp[i].frame;
            mi = i;
        }
    }
    if (found != -1) {
        unsigned int wait = sp->se_id == 0x33 ? 0x14 : 4;
        if (FRAME < s_seexp[found].frame + wait) return;
        Voice_CallPoint(sp->se_id2, sp->se_vol2, &ap->matrix[3], sp->se_dist2);
        s_seexp[found].frame = FRAME;
    } else {
        Voice_CallPoint(sp->se_id2, sp->se_vol2, &ap->matrix[3], sp->se_dist2);
        s_seexp[mi].id = ap->spell->se_id2;
        s_seexp[mi].frame = FRAME;
    }
}

/* kills every live missile-type spell (pids 4, 5, 6, 8, 0xa, 0x15, 0x1e) of the shooter kind with an explosion */
void DF_SPELL2_KillMissile_Kind(int kind)
{
    PS2_FRAME(0x40);
    _spell2 *p = sap0;
    for (int i = 0; i < 0x200; i++, p++) {
        if (!(p->on > 0)) continue;
        int pid = p->sdp->pid;
        if (!s_SpellFunc[pid].kind || p->flag.kind != kind) continue;
        if (pid == 0x1e || pid == 0x15 || pid == 0xa || pid == 8 || pid == 6 || pid == 5 || pid == 4) {
            DF_MAGIC2_AppearMagic(0x30, p->matrix[3], p->ang, 0, 0, 0, 0, 0);
            kill(p);
        }
    }
}

/* common kill: on = -2 (freed two SpellProc passes later), voice, effect family, second collision entry, smoke */
static void __spell_KillSpellProc(_spell2 *ap)
{
    PS2_FRAME(0x20);
    if (!ap || !(ap->on > 0)) return;
    ap->on = -2;
    if ((unsigned int)(int)ap->snd_id < 0x50) {
        Voice_Stop(ap->snd_id, 0);
        ap->snd_id = 0x50;
    }
    if (ap->id) {
        DF_MAGIC2_Kill_ID(ap->id);
        ap->id = 0;
    }
    DF_COLL_Delete(ap->hcol2);
    ap->hcol2 = -1;
    if (s_SpellFunc[ap->sdp->pid].use_smoke == 1 && ap->dat.missile.smoke_id) DF_MAGIC3_Kill(ap->dat.missile.smoke_id);
}

void DF_SPELL2_KillSpell(_spell2 *ap)
{
    PS2_FRAME(0x10);
    if (!ap || !(ap->on > 0)) return;
    kill(ap);
}

/* a free slot (on = its index while it is set up) */
static _spell2 *spell_alloc(void)
{
    _spell2 *p = sap0 + 1;
    int i;
    for (i = 1; i < 0x200; i++, p++)
        if (!p->on) break;
    if (!(i < 0x200)) return 0;
    p->on = (short)i;
    short on = p->on;
    ZeroMemory32b(p, 0x48);
    p->on = on;
    p->pos[3] = 1.0f;
    p->ang[3] = 1.0f;
    p->snd_id = 0x50;
    p->id = 0;
    p->FRAME = FRAME;
    p->hcol = -1;
    p->hcol2 = -1;
    return p;
}

static void spell_shooter(_spell2 *p, char kind, char id)
{
    p->flag.kind = kind & 3;
    p->flag.id = (int)id & 0x3ff;
}

static int comb_bad(const _stSpell2Data *sdp, const _stSfxCombHead *comb)
{
    unsigned int idx = (PS2PTR(sdp) - comb->spell_ofs) / 0xc0;
    return !((int)idx < comb->spell_num);
}

static void parabolic_next(const _stSpell2Data *sdp)
{
    if (sdp->pid != 0xb) return;
    s_ParabolicID = s_ParabolicID < 0xffff ? s_ParabolicID + 1 : 1;
}

_spell2 *DF_SPELL2_AppearSpell2Ptr(_stSpell2Data *sdp, char shooter_kind, char shooter_id, SfxDstat *dstat,
                                   _stSfxCombHead *comb, ObjCMatSt *ocmp, float *target, void *argp)
{
    PS2_FRAME(0xd0);
    _spell2 *p = 0;
    if (comb && comb_bad(sdp, comb)) return 0;
    if (!sdp || !sdp->pid) return 0;
    s_MazzleFlag = 0;
    for (int n = 0; n < (sdp->with > 0 ? sdp->with : 1); n++) {
        p = spell_alloc();
        if (!p) return 0;
        p->sdp = sdp;
        if (comb) p->comb = comb;
        spell_shooter(p, shooter_kind, shooter_id);
        if (ocmp) p->ocmp = ocmp;
        p->dstat = dstat ? dstat : &p->sdp->dstat;
        p->spell = &p->sdp->spell;
        if (target) copy4(p->target, target);
        p->argp = argp;
        gcall(s_SpellFunc[p->sdp->pid].initfunc, p);
        p->myframe = (int)FRAME;
        if (sdp->pid == 0xb) p->dat.parabolic.spell_id = s_ParabolicID;
        __SpellFireSet(shooter_kind, shooter_id, 0);
        s_MazzleFlag = 1;
    }
    parabolic_next(sdp);
    return p;
}

_spell2 *DF_SPELL2_AppearSpell2(short spell_id, char shooter_kind, char shooter_id, SfxDstat *dstat, ObjCMatSt *ocmp,
                                float *target, void *argp)
{
    PS2_FRAME(0x10);
    if (!spell_id) return 0;
    return DF_SPELL2_AppearSpell2Ptr(SpellData2 + spell_id, shooter_kind, shooter_id, dstat, 0, ocmp, target, argp);
}

/* flag: 2 = only a 0x3d8 / 0x8000 chance, 0x80 = ignore target; the copies after the first get flags | 8 (no
 * second voice) */
_spell2 *DF_SPELL2_AppearSpellPtr(_stSpell2Data *sdp, char shooter_kind, char shooter_id, SfxDstat *dstat,
                                  struct_88b6ff44 *spell, _stSfxCombHead *comb, float *pos, float *ang, float *target,
                                  ObjCMatSt *ocmp, void *argp, unsigned short flag, unsigned short wid)
{
    PS2_FRAME(0xf0);
    _spell2 *p = 0;
    if (comb && comb_bad(sdp, comb)) return 0;
    if (!sdp || !sdp->pid) return 0;
    if ((flag & 2) && !rand_pass(0x3d8)) return 0;
    s_MazzleFlag = 0;
    unsigned short n = sdp->with > 0 ? sdp->with : 1;
    for (int i = 0; i < n; i++) {
        p = spell_alloc();
        if (!p) return 0;
        p->sdp = sdp;
        if (comb) p->comb = comb;
        spell_shooter(p, shooter_kind, shooter_id);
        if (pos) copy4(p->pos, pos);
        if (ang) copy4(p->ang, ang);
        if (ocmp) p->ocmp = ocmp;
        p->argp = argp;
        p->flags = flag;
        if (i > 0) p->flags |= 8;
        p->weapon_id = wid;
        p->dstat = dstat ? dstat : &p->sdp->dstat;
        p->spell = spell ? spell : &p->sdp->spell;
        if (!(flag & 0x80) && target) copy4(p->target, target);
        gcall(s_SpellFunc[p->sdp->pid].initfunc, p);
        p->myframe = (int)FRAME;
        if (sdp->pid == 0xb) p->dat.parabolic.spell_id = s_ParabolicID;
        __SpellFireSet(shooter_kind, shooter_id, flag);
        s_MazzleFlag = 1;
    }
    parabolic_next(sdp);
    return p;
}

_spell2 *DF_SPELL2_AppearSpell(short spell_id, char shooter_kind, char shooter_id, SfxDstat *dstat,
                               struct_88b6ff44 *spell, float *pos, float *ang, float *target, ObjCMatSt *ocmp,
                               void *argp, unsigned short flag, unsigned short wid)
{
    PS2_FRAME(0x40);
    if (!spell_id) return 0;
    return DF_SPELL2_AppearSpellPtr(SpellData2 + spell_id, shooter_kind, shooter_id, dstat, spell, 0, pos, ang, target,
                                    ocmp, argp, flag, wid);
}

#ifndef ACNB_SOURCEPORT
/* MIPS -> native wrappers (> 8 int args; stack arguments in the caller's 8-byte slots from sp+0) */
static void bw__DF_SPELL2_AppearSpellPtr(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    _stSpell2Data *sdp = (_stSpell2Data *)bridge_ptr(c->gpr[A0].uw[0]);
    char kind = (char)c->gpr[A1].uw[0], id = (char)c->gpr[A2].uw[0];
    SfxDstat *dstat = (SfxDstat *)bridge_ptr(c->gpr[A3].uw[0]);
    struct_88b6ff44 *spell = (struct_88b6ff44 *)bridge_ptr(c->gpr[T0].uw[0]);
    _stSfxCombHead *comb = (_stSfxCombHead *)bridge_ptr(c->gpr[T1].uw[0]);
    float *pos = (float *)bridge_ptr(c->gpr[T2].uw[0]);
    float *ang = (float *)bridge_ptr(c->gpr[T3].uw[0]);
    float *target = (float *)bridge_ptr(rd32(sp));
    ObjCMatSt *ocmp = (ObjCMatSt *)bridge_ptr(rd32(sp + 8));
    void *argp = bridge_ptr(rd32(sp + 0x10));
    unsigned short flag = (unsigned short)rd32(sp + 0x18), wid = (unsigned short)rd32(sp + 0x20);
    _spell2 *r = DF_SPELL2_AppearSpellPtr(sdp, kind, id, dstat, spell, comb, pos, ang, target, ocmp, argp, flag, wid);
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}

static void bw__DF_SPELL2_AppearSpell(EECore *c)
{
    u32 sp = c->gpr[SP].uw[0];
    short spell_id = (short)c->gpr[A0].uw[0];
    char kind = (char)c->gpr[A1].uw[0], id = (char)c->gpr[A2].uw[0];
    SfxDstat *dstat = (SfxDstat *)bridge_ptr(c->gpr[A3].uw[0]);
    struct_88b6ff44 *spell = (struct_88b6ff44 *)bridge_ptr(c->gpr[T0].uw[0]);
    float *pos = (float *)bridge_ptr(c->gpr[T1].uw[0]);
    float *ang = (float *)bridge_ptr(c->gpr[T2].uw[0]);
    float *target = (float *)bridge_ptr(c->gpr[T3].uw[0]);
    ObjCMatSt *ocmp = (ObjCMatSt *)bridge_ptr(rd32(sp));
    void *argp = bridge_ptr(rd32(sp + 8));
    unsigned short flag = (unsigned short)rd32(sp + 0x10), wid = (unsigned short)rd32(sp + 0x18);
    _spell2 *r = DF_SPELL2_AppearSpell(spell_id, kind, id, dstat, spell, pos, ang, target, ocmp, argp, flag, wid);
    c->gpr[V0].sd[0] = (s32)PS2PTR(r);
}
#endif

void DF_SPELL2_Init(void)
{
    PS2_FRAME(0x10);
    sap0 = (_spell2 *)Memm_Alloc(1, 0x24000, 0x10);
    ZeroMemory32b(sap0, 0x9000);
    s_ParabolicID = 1;
    s_FunnelID = 0;
    ZeroMemory32b(s_pNowFrameDecoy, 0x200);
    s_NowFrameDecoy = 0;
    s_ThunderFlag = 0;
    ZeroMemory32b(AC_nFire, 9);
    ZeroMemory32b(AC_nHit, 9);
    Ene_nFire = 0;
    Ene_nHit = 0;
    g_nEneHitBullet = 0;
    ZeroMemory(s_seexp, 0x20);
}

#ifdef ACNB_SOURCEPORT
float __spell_MainProg05__speed_rate[2] = {1.0f, 2.0f};   /* main 0x278ff0 */
/* the original's initialized tables (generated by scripts/sp_datagen.py from the PS2 image) */
struct struct_dc0108f0 s_SpellFunc[32] = {   /* main 0x2601a0 */
    {(void (*)(struct _spell2 *))__spell_InitProgxx, __spell_MainProgxx, (void (*)(void *, void *,
     int))__spell_HitProgxx, (void (*)(struct _spell2 *))__spell_KillProgxx, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0}},
    {__spell_InitProg01, __spell_MainProg01, __spell_HitProg01, __spell_KillProg01, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg02, __spell_MainProg02, __spell_HitProg02, __spell_KillProg02, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg03, __spell_MainProg02, __spell_HitProg02, __spell_KillProg03, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg04, __spell_MainProg04, __spell_HitProg02, __spell_KillProg04, 1, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg04, __spell_MainProg05, __spell_HitProg02, __spell_KillProg04, 1, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg06, __spell_MainProg06, __spell_HitProg02, __spell_KillProg02, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg07, __spell_MainProg07, (void (*)(void *, void *, int))__spell_HitProgxx, __spell_KillProg07,
     0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {__spell_InitProg08, __spell_MainProg08, __spell_HitProg02, __spell_KillProg04, 1, 0, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg09, __spell_MainProg09, __spell_HitProg16, __spell_KillProg01, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg10, __spell_MainProg10, __spell_HitProg10, __spell_KillProg01, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg11, __spell_MainProg11, __spell_HitProg02, __spell_KillProg01, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg12, __spell_MainProg12, (void (*)(void *, void *, int))__spell_HitProgxx, __spell_KillProg07,
     0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {__spell_InitProg13, __spell_MainProg13, __spell_HitProg02, __spell_KillProg13, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg14, __spell_MainProg14, __spell_HitProg02, __spell_KillProg14, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg15, __spell_MainProg15, __spell_HitProg16, __spell_KillProg15, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg16, __spell_MainProg16, __spell_HitProg16, __spell_KillProg01, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg17, __spell_MainProg17, __spell_HitProg16, __spell_KillProg17, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg18, __spell_MainProg18, (void (*)(void *, void *, int))__spell_HitProgxx, __spell_KillProg18,
     0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {__spell_InitProg19, __spell_MainProg19, __spell_HitProg19, __spell_KillProg19, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg20, __spell_MainProg04, __spell_HitProg02, __spell_KillProg04, 1, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg04, __spell_MainProg04, __spell_HitProg21, __spell_KillProg04, 1, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg22, __spell_MainProg22, __spell_HitProg22, __spell_KillProg01, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg23, __spell_MainProg23, (void (*)(void *, void *, int))__spell_HitProgxx, __spell_KillProg23,
     0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {__spell_InitProg24, __spell_MainProg11, __spell_HitProg24, __spell_KillProg01, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg25, __spell_MainProg02, __spell_HitProg02, __spell_KillProg02, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg04, __spell_MainProg26, __spell_HitProg02, __spell_KillProg04, 1, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg02, __spell_MainProg27, __spell_HitProg02, __spell_KillProg02, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg04, __spell_MainProg28, __spell_HitProg02, __spell_KillProg04, 1, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg29, __spell_MainProg29, __spell_HitProg29, __spell_KillProg29, 0, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {__spell_InitProg04, __spell_MainProg04, __spell_HitProg02, __spell_KillProg04, 1, 1, {0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0}},
    {(void (*)(struct _spell2 *))__spell_InitProgxx, __spell_MainProgxx, (void (*)(void *, void *,
     int))__spell_HitProgxx, (void (*)(struct _spell2 *))__spell_KillProgxx, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0}},
};
#endif
