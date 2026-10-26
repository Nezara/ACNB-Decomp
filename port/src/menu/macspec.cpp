/* src/menu/macspec.cpp — the "AC spec" shown on the garage / assembly screens (OLMENU; Mac = menu AC): AP, WP, EP,
 * OP, DP, price, stability, mobility, cooling, radar / sensor / FCS points, lock-on property, ammo, grade (rank 0..4),
 * info flags and the radar-graph values. DF_MACSPEC_Get fills a struct_dbbd570b from an stMenuAC; it points the
 * global ACSpecPartsP at a temporary set of PartsParam records (tuned copies) and every __Calc_* static reads the
 * parts through it, in a fixed order (Grade / Check_Info read the fields the earlier calcs wrote).
 * Whole file native. The tuned copies are locals of DF_MACSPEC_Get at their original stack slots (PS2_STACK): their
 * addresses go into the heap block ACSpecPartsP points at, which stays dangling after Get returns (as in the original). */
#include "acnb/hooks.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

/* u16 at +4 of the DF_MDATA_GetData record of ACp->cur_part[n] (entry k); the original reads it without a null
 * test: a missing part (or a 0 record) reads PS2 address 4. */
static inline const unsigned char *macspec_mdata(struct stMenuAC *ACp, int n, int k)
{
    struct _stMModel *m = ACp->cur_part[n];
    if (!m) return 0;
    /* asm: lw 0x50(mmdp) with no test of mmdp (PS2 address 0x50 when null) */
    const struct stMmdData *md = m->mmdp ? m->mmdp : (const struct stMmdData *)PS2_NULL_MEM();
    return (const unsigned char *)DF_MDATA_GetData(md->OTH.datap, k);
}

static inline float macspec_u16_4(const unsigned char *d)
{
    if (!d) d = (const unsigned char *)PS2_NULL_MEM();
    return (float)(int)*(const unsigned short *)(d + 4);
}

/* graph_ammo: core / arm / gun / back-weapon ammo, scaled by the back magazine multiplier, 1..100 */
static void __Calc_GraphAmmo(struct struct_dbbd570b *outSpecp, struct stMenuAC *ACp)
{
    PS2_FRAME(0x40);
    float mag = 1.0f;
    float sum = 0.0f;
    float core = 0.0f;
    float sword = 0.0f;

    if (ACSpecPartsP->corep && outSpecp->core_type == 1)
        core = macspec_u16_4(macspec_mdata(ACp, 1, 1));
    if (ACSpecPartsP->armsp && ACSpecPartsP->armsp->type == 1) {
        float a = macspec_u16_4(macspec_mdata(ACp, 2, 1));
        sum = sum + a;
        float b = macspec_u16_4(macspec_mdata(ACp, 2, 2));
        sum = sum + b;
        sum = sum / 2.0f;
    }
    if (ACSpecPartsP->gunp)
        sum = sum + macspec_u16_4(macspec_mdata(ACp, 16, 1));
    if (ACSpecPartsP->back_rp) {
        const unsigned char *d = macspec_mdata(ACp, 14, 1);
        struct PartsParamBackSt *b = ACSpecPartsP->back_rp;
        if (b->type == 0)
            sum = sum + macspec_u16_4(d);
        else if (b->type == 3 || b->type == 4)
            mag = (float)(int)b->magazine_add;
    }
    if (ACSpecPartsP->back_lp) {
        const unsigned char *d = macspec_mdata(ACp, 15, 1);
        struct PartsParamBackSt *b = ACSpecPartsP->back_lp;
        if (b->type == 0)
            sum = sum + macspec_u16_4(d);
        else if (b->type == 3 || b->type == 4)
            mag = (float)(int)b->magazine_add;
    }
    if (ACSpecPartsP->swordp)
        sword = 10.0f;

    float f = sum * mag;
    f = core + f;
    f = f / 600.0f; /* __Calc_GraphAmmo__max */
    f = 90.0f * f;
    f = sword + f;
    int v = (int)f;
    if (v < 2) v = 1;
    if (v >= 100) v = 100;
    outSpecp->graph_ammo = v;
}

/* lockon_proper: per lock-capable weapon, (FCS sight range / weapon range) x (sight lock angle / total lock angle) */
static void __Calc_LockonProper(struct struct_dbbd570b *outSpecp, struct stMenuAC *ACp)
{
    PS2_FRAME(0x90);
    static const int slots[7] = {2, 16, 17, 14, 15, 18, 19};

    outSpecp->lockon_proper = 0;
    struct PartsParamFcsSt *fcs = ACSpecPartsP->fcsp;
    if (!fcs) return;

    float total = 0.0f;
    total = total + (float)(fcs->sight[0].lock_angx + fcs->sight[0].lock_angy);
    total = total + (float)(fcs->sight[1].lock_angx + fcs->sight[1].lock_angy);
    total = total + (float)(fcs->sight[2].lock_angx + fcs->sight[2].lock_angy);
    total = total + (float)(fcs->sight[3].lock_angx + fcs->sight[3].lock_angy);

    float rsum = 0.0f;
    float asum = 0.0f;
    int cnt = 0;
    for (int i = 0; i < 7; i++) {
        int slot = slots[i];
        const unsigned char *d = macspec_mdata(ACp, slot, 1);
        if (!d) continue;
        struct struct_0ccde9cd *P = ACSpecPartsP;
        if (slot == 19 || slot == 17) {
            struct PartsParamSwordSt *s = slot == 17 ? P->swordp : P->spswordp;
            if (!P->armsp || P->armsp->type != 0 || !s || s->type != 3 || s->basic.category_id == 0x15) continue;
        } else if (slot == 18 || slot == 16) {
            struct PartsParamGunSt *g = slot == 16 ? P->gunp : P->spgunp;
            if ((signed char)d[3] == 2 || !g || g->type == 3) continue;
        } else if (slot == 15) {
            if (P->f_wback || P->f_back_one) continue;
            /* no null test of back_lp */
            const struct PartsParamBackSt *b = P->back_lp ? P->back_lp : (const struct PartsParamBackSt *)PS2_NULL_MEM();
            if (b->type != 0 || (signed char)d[3] == 2) continue;
        } else if (slot == 14) {
            /* no null test of back_rp */
            const struct PartsParamBackSt *b = P->back_rp ? P->back_rp : (const struct PartsParamBackSt *)PS2_NULL_MEM();
            if (b->type != 0 || (signed char)d[3] == 2) continue;
        } else if (slot == 2) {
            if (!P->armsp || P->armsp->type != 1) continue;
        }

        /* d[8] is an unbounded sight index (> 3 reads other FCS fields) */
        const unsigned char *sight = (const unsigned char *)ACSpecPartsP->fcsp + 0x110 + d[8] * 0x10;
        float wrange = *(const float *)(d + 0x58);
        float r = 0.0f;
        if (!(wrange == 0.0f)) r = *(const float *)sight / wrange;
        if (r < 0.0f) r = 0.0f;
        if (!(r <= 1.0f)) r = 1.0f;
        rsum = rsum + r;
        float a = 0.0f;
        if (!(total == 0.0f)) {
            sight = (const unsigned char *)ACSpecPartsP->fcsp + 0x110 + d[8] * 0x10;
            a = (float)(*(const short *)(sight + 8) + *(const short *)(sight + 0xa)) / total;
        }
        cnt++;
        asum = asum + a;
    }
    if (cnt > 0) {
        float n = (float)cnt;
        rsum = rsum / n;
        asum = asum / n;
    }
    outSpecp->lockon_proper = (int)(10000.0f * (rsum * asum));
}

/* info: 0x10 no core, 1 missing parts, 2 legs overweight, 4 core overweight, 8 EP over */
static void __Check_Info(struct struct_dbbd570b *outSpecp)
{
    outSpecp->info = 0;
    struct struct_0ccde9cd *P = ACSpecPartsP;
    if (!P->corep) {
        outSpecp->info |= 0x10;
        return;
    }
    if (!P->headp || !P->armsp || !P->legsp || !P->fcsp || !P->genep || !P->radp)
        outSpecp->info |= 1;
    P = ACSpecPartsP;
    if (!P->boostp && P->legsp && P->legsp->type != 3 && P->legsp->type != 4)
        outSpecp->info |= 1;
    if (outSpecp->legs_wp_max < outSpecp->legs_wp) outSpecp->info |= 2;
    if (outSpecp->core_wp_max < outSpecp->core_wp) outSpecp->info |= 4;
    if (outSpecp->ep_max < outSpecp->ep) outSpecp->info |= 8;
}

/* one grade metric: clamp to [lo, hi], scale onto its share of the 1000-point score */
static inline float macspec_grade_term(int v, float lo, float hi, float scale, float den, float base)
{
    float x = (float)v;
    if (x <= lo) x = lo;
    if (!(x < hi)) x = hi;
    float t = scale * (x - lo);
    t = t / den;
    return base + t;
}

/* grade 0..4 from a 0..1000 score over twelve spec values */
static void __Calc_Grade(struct struct_dbbd570b *ioSpecp)
{
    static const float base[4] = {140.0f, 150.0f, 165.0f, 190.0f}; /* __Calc_Grade__base (5th, 10000, unread) */
    float acc = 0.0f;
    acc = acc + macspec_grade_term(ioSpecp->ap, 4100.0f, 9900.0f, 21.0f, 5800.0f, 24.0f);
    acc = acc + macspec_grade_term(ioSpecp->surplus_ep, 0.0f, 6500.0f, 30.0f, 6500.0f, 0.0f);
    acc = acc + macspec_grade_term(ioSpecp->op, 0.0f, 19000.0f, 90.0f, 19000.0f, 0.0f);
    acc = acc + macspec_grade_term(ioSpecp->dp, 1900.0f, 4000.0f, 24.0f, 2100.0f, 16.0f);
    acc = acc + macspec_grade_term(ioSpecp->stability, 1400.0f, 10500.0f, 20.0f, 9100.0f, 0.0f);
    acc = acc + macspec_grade_term(ioSpecp->moving, 150.0f, 1600.0f, 40.0f, 1450.0f, 7.0f);
    acc = acc + macspec_grade_term(ioSpecp->turning, 107.0f, 145.0f, 15.0f, 38.0f, 5.0f);
    acc = acc + macspec_grade_term(ioSpecp->rising, 600.0f, 5600.0f, 20.0f, 5000.0f, 5.0f);
    acc = acc + macspec_grade_term(ioSpecp->cooling, 5400.0f, 15000.0f, 10.0f, 9600.0f, 0.0f);
    acc = acc + macspec_grade_term(ioSpecp->radar_p, 0.0f, 2250.0f, 16.0f, 2250.0f, 0.0f);
    acc = acc + macspec_grade_term(ioSpecp->sensor_p, 300.0f, 650.0f, 10.0f, 350.0f, 0.0f);
    acc = acc + macspec_grade_term(ioSpecp->fcs_p, 770.0f, 3800.0f, 12.0f, 3030.0f, 0.0f);
    if (acc <= 0.0f) acc = 0.0f;
    if (!(acc < 1000.0f)) acc = 1000.0f;
    int i = 0;
    do {
        if (!(base[i] <= acc)) break;
        i++;
    } while (i < 4);
    ioSpecp->grade = i;
}

/* radar_p / sensor_p / fcs_p: head radar merged with radar back units, sensor flags, FCS sight sums */
static void __Calc_Support(struct struct_dbbd570b *ioSpecp)
{
    PS2_FRAME(0x50);
    PS2_STACK(struct PartsParamBasicRadarSt, radar, 0x40);
    unsigned char addon = 0;
    float sensor = 0.0f;
    float fcs_p = 0.0f;
    float range = 0.0f;
    float ltime = 0.0f;

    DF_MFUNC_ClearMemoryC(&radar, 0x10);
    struct struct_0ccde9cd *P = ACSpecPartsP;
    struct PartsParamHeadSt *h = P->headp;
    if (h && (h->addon & 1)) {
        const unsigned int *src = (const unsigned int *)&h->radar_stat;
        unsigned int *dst = (unsigned int *)&radar;
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
        addon = P->headp->addon;
    }
    for (int i = 0; i < 2; i++) {
        struct PartsParamBackSt *b = i == 0 ? P->back_lp : P->back_rp;
        if (!b || b->type != 1) continue;
        if (radar.rd_range < b->radar_stat.rd_range) radar.rd_range = b->radar_stat.rd_range;
        if ((int)b->radar_stat.rd_intvl < (int)radar.rd_intvl) radar.rd_intvl = b->radar_stat.rd_intvl;
        radar.stat = radar.stat | b->radar_stat.stat;
    }
    float rp = 60.0f * radar.rd_range;
    rp = rp / (float)((int)radar.rd_intvl + 30);
    unsigned char st = radar.stat;
    sensor = sensor + ((st & 1) ? 100.0f : 50.0f);
    sensor = sensor + ((st & 4) ? 100.0f : 50.0f);
    sensor = sensor + ((st & 8) ? 100.0f : 50.0f);
    sensor = sensor + ((st & 0x10) ? 100.0f : 50.0f);
    if (st & 0x20)
        sensor = sensor + 150.0f;
    else
        sensor = sensor + ((addon & 8) ? 100.0f : 50.0f);

    struct PartsParamFcsSt *fc = ACSpecPartsP->fcsp;
    if (fc) {
        range = range + fc->sight[0].range;
        range = range + fc->sight[1].range;
        range = range + fc->sight[2].range;
        ltime = ltime + (float)fc->sight[0].lock_time;
        ltime = ltime + (float)fc->sight[1].lock_time;
        range = range + fc->sight[3].range;
        range = range + fc->bladelock_range;
        ltime = ltime + (float)fc->sight[2].lock_time;
        ltime = ltime + (float)fc->sight[3].lock_time;
        float lt = 10.0f * ltime;
        fcs_p = range + (float)(fc->lockmax * 100);
        fcs_p = fcs_p - lt;
        fcs_p = fcs_p + (float)(fc->precision * 100);
    }
    /* cvt.w.s, then the low 16 bits sign-extended */
    ioSpecp->radar_p = (short)(int)rp;
    ioSpecp->sensor_p = (short)(int)sensor;
    ioSpecp->fcs_p = (short)(int)fcs_p;
}

/* cooling / recov_cooling: radiator values + the parts' defense cooling (option 0x40000 scales the parts' part) */
static void __Calc_Cooling(struct struct_dbbd570b *ioSpecp)
{
    struct struct_0ccde9cd *P = ACSpecPartsP;
    float c = 0.0f;
    float r = 0.0f;
    float d = 0.0f;
    if (P->radp) {
        c = c + (float)(int)P->radp->cooling;
        r = r + (float)(int)P->radp->recov_cooling;
    }
    if (P->headp) d = d + (float)(int)P->headp->defense.cooling;
    if (P->corep) d = d + (float)(int)P->corep->defense.cooling;
    if (P->armsp) d = d + (float)(int)P->armsp->defense.cooling;
    if (P->legsp) d = d + (float)(int)P->legsp->defense.cooling;
    if (P->option & 0x40000) d = d * g_asmparam.opt_cool_up;
    ioSpecp->cooling = (int)(c + d);
    ioSpecp->recov_cooling = (int)(r + d);
}

/* moving / turning / rising, boost_time, boost_max_speed, rot_speed from legs / booster / generator / back booster */
static void __Calc_Mobility(struct struct_dbbd570b *ioSpecp)
{
    PS2_FRAME(0x40);
    struct struct_0ccde9cd *P = ACSpecPartsP;
    struct PartsParamLegsSt *legs = P->legsp;
    float moving = 0.0f;
    float turning = 0.0f;
    if (legs) {
        moving = (float)(int)legs->speed;
        if (P->corep && P->f_ob)
            moving = moving + (float)(int)(0.2f * (float)(int)P->corep->ob_power);
    }

    float bpow = 0.0f;
    int bsub = 0;
    if (legs) {
        unsigned char t = legs->type;
        if (t == 4 || t == 3) {
            bpow = bpow + (float)(int)legs->boost_power;
            bsub += legs->boost_sub;
        } else if (t == 2 || t == 1 || t == 0) {
            struct PartsParamBoosterSt *b = P->boostp;
            if (b) {
                bpow = bpow + (float)(int)b->power;
                bsub += b->boost_sub;
            }
        }
    }
    /* back booster: left slot only, signed boost_add */
    if (P->back_lp && P->back_lp->type == 5)
        bpow = bpow + (float)(int)P->back_lp->boost_add;

    float jump = legs ? (float)(int)legs->jumpspd : 0.0f;
    float lw = ps2_div(bpow, (float)ioSpecp->wp);
    /* mula.s / madd.s: both products rounded, then added (runs with jump = 0 when there are no legs) */
    float p1 = 1000.0f * lw;
    float p2 = 1.5f * jump;
    float rising = p1 + p2;
    if (legs) turning = (float)(int)legs->rot_speed;

    int charge = 0;
    if (P->genep) {
        struct struct_0ccde9cd *Q = ACSpecPartsP;
        struct PartsParamGeneratorSt *g = Q->genep;
        charge += g->charge_max + 3 * g->charge_recov;
        if (Q->option & 8)
            charge = (int)((float)charge + g_asmparam.opt_cond_add);
    }
    if (bsub == 0) {
        ioSpecp->boost_time = 0;
    } else {
        float m = (float)bsub * g_asmparam.prog_boostsub_rate;
        m = 0.5f * m;
        m = 60.0f * m;
        ioSpecp->boost_time = (int)ps2_div(1000.0f * (float)charge, m);
    }

    ioSpecp->boost_max_speed = 0;
    ioSpecp->rot_speed = 0;
    legs = ACSpecPartsP->legsp;
    if (legs) {
        int wp = ioSpecp->wp;
        float wr = ((float)wp * g_asmparam.decrease_bstspd_by_weight) / 1000.0f;
        float bw = g_asmparam.bstspd_weight_rate_default - wr;
        if (bw < g_asmparam.boost_weightrate_min) bw = g_asmparam.boost_weightrate_min;
        if (!(bw <= g_asmparam.boost_weightrate_max)) bw = g_asmparam.boost_weightrate_max;

        float ow = AcAsm_GetOwRateFromTable(ps2_div((float)wp, (float)(int)(legs->basic.wp + legs->max_wp)));
        float tr = 1.0f - 0.5f * (1.0f - ow);
        if (tr < g_asmparam.ow_leg_turn_rate_min) tr = g_asmparam.ow_leg_turn_rate_min;

        P = ACSpecPartsP;
        float rs = (float)(int)P->legsp->rot_speed;
        if (P->option & 0x100) rs = rs * g_asmparam.opt_turn_acc;
        float q = AcAsm_RateRectify(ps2_div(rs, g_asmparam.parts_turn_stdspd), g_asmparam.turnrate_difhosei,
                                    g_asmparam.turnrate_min, g_asmparam.turnrate_max);
        ioSpecp->rot_speed = (int)(1000.0f * (60.0f * (g_asmparam.prog_turn_stdspd * (tr * q))));

        if (ow < g_asmparam.ow_leg_speed_rate_min) ow = g_asmparam.ow_leg_speed_rate_min;
        legs = ACSpecPartsP->legsp;
        if (legs->type == 3) {
            q = AcAsm_RateRectify(ps2_div((float)(int)legs->speed, g_asmparam.parts_avr_movespd),
                                  g_asmparam.moverate_difhosei, g_asmparam.moverate_min, g_asmparam.moverate_max);
            float s = g_asmparam.prog_move_stdmaxspd * q;
            s = ow * s;
            s = 60.0f * s;
            s = 60.0f * s;
            s = 60.0f * s;
            ioSpecp->boost_max_speed = (int)(s / 1000.0f);
        } else if (!(bpow <= 0.0f)) {
            q = AcAsm_RateRectify(ps2_div(bpow, g_asmparam.parts_boost_max), g_asmparam.boost_powerrate,
                                  g_asmparam.boost_powerrate_min, g_asmparam.boost_powerrate_max);
            float s = g_asmparam.std_dash_maxspd * bw;
            s = q * s;
            s = ow * s;
            s = 60.0f * s;
            s = 60.0f * s;
            s = 60.0f * s;
            ioSpecp->boost_max_speed = (int)(s / 1000.0f);
        }
    }
    /* cvt.w.s, then the low 16 bits sign-extended */
    ioSpecp->moving = (short)(int)moving;
    ioSpecp->turning = (short)(int)turning;
    ioSpecp->rising = (short)(int)rising;
}

/* stability: legs overweight rate x legs balance x head balance (0 without legs or head) */
static void __Calc_Stability(struct struct_dbbd570b *ioSpecp)
{
    PS2_FRAME(0x20);
    struct struct_0ccde9cd *P = ACSpecPartsP;
    struct PartsParamLegsSt *legs = P->legsp;
    if (!legs || !P->headp) {
        ioSpecp->stability = 0;
        return;
    }
    float r = AcAsm_GetOwRateFromTable(ps2_div((float)ioSpecp->wp, (float)(int)(legs->basic.wp + legs->max_wp)));
    float d = 1.0f - r;
    d = d * d;
    float f3 = (r <= 1.0f) ? 1.0f - d : 1.0f + d;
    if (f3 < g_asmparam.ow_leg_brake_rate_min) f3 = g_asmparam.ow_leg_brake_rate_min;
    float f2 = (ACSpecPartsP->option & 4) ? g_asmparam.opt_dmgbal_rate : 1.0f;
    P = ACSpecPartsP;
    int n = (int)P->legsp->balance * (int)P->headp->balance;
    float s = f3 * (float)n;
    s = f2 * s;
    ioSpecp->stability = (int)(0.01f * s);
}

/* price: parts prices (slot 11 only without twin / one-sided back) + option parts */
static void __Calc_Price(struct struct_dbbd570b *ioSpecp)
{
    PS2_FRAME(0x30);
    ioSpecp->price = 0;
    for (int i = 0; i < 11; i++) {
        struct PartsParamBasicSt *p = ACSpecPartsP->pbp[i];
        if (p) ioSpecp->price += p->price;
    }
    struct struct_0ccde9cd *P = ACSpecPartsP;
    if (!P->f_wback && P->pbp[11] && !P->f_back_one) ioSpecp->price += P->pbp[11]->price;
    if (ACSpecPartsP->pbp[12]) ioSpecp->price += ACSpecPartsP->pbp[12]->price;
    if (ACSpecPartsP->pbp[13]) ioSpecp->price += ACSpecPartsP->pbp[13]->price;
    for (int s = 0; s < 32; s++) {
        if (ACSpecPartsP->option & (1u << s))
            ioSpecp->price += DF_MPART_GetOPPrice(s + 1);
    }
}

/* one OP term from a DF_MDATA_GetData record: u16 0x1a (x opt_e_power for flag 0x20 under option 0x800), x u16 4
 * unless that is 0 */
static inline int macspec_op_value(const unsigned char *d)
{
    if (!d) return 0;
    float f = (float)(int)*(const unsigned short *)(d + 0x1a);
    if ((*(const unsigned short *)(d + 0x18) & 0x20) && (ACSpecPartsP->option & 0x800))
        f = f * g_asmparam.opt_e_power;
    unsigned short n = *(const unsigned short *)(d + 4);
    if (n == 0) return (int)f;
    return (int)(f * (float)(int)n);
}

static inline int macspec_op_term(struct stMenuAC *ACp, int slot)
{
    return macspec_op_value(macspec_mdata(ACp, slot, 1));
}

/* sword slot: laser blade (type 3) from its record, blades 0 / 1 from 5 x blade damage (option 0x400 scales it) */
static inline void macspec_op_sword(struct struct_dbbd570b *outSpecp, struct stMenuAC *ACp,
                                    struct PartsParamSwordSt *s, int slot)
{
    if (!s) return;
    unsigned char t = s->type;
    if (t == 3) {
        outSpecp->op += macspec_op_term(ACp, slot);
    } else if (t == 1 || t == 0) {
        int v = s->blade_dstat.damage; /* loaded on both paths: no "else 0" */
        if (ACSpecPartsP->option & 0x400) v = (int)((float)v * g_asmparam.opt_bladeup);
        outSpecp->op += v * 5;
    }
}

/* op: offence points of every weapon slot, / 10 */
static void __Calc_OP(struct struct_dbbd570b *outSpecp, struct stMenuAC *ACp)
{
    PS2_FRAME(0x40);
    outSpecp->op = 0;
    struct PartsParamCoreSt *core = ACSpecPartsP->corep;
    if (!core) return;
    if (core->func_type == 0) outSpecp->op += macspec_op_term(ACp, 1);

    struct PartsParamArmsSt *a = ACSpecPartsP->armsp;
    if (a && (a->type == 1 || a->type == 2)) {
        const unsigned char *d1 = macspec_mdata(ACp, 2, 1);
        const unsigned char *d2 = macspec_mdata(ACp, 2, 2);
        int s = macspec_op_value(d1);
        s = s + macspec_op_value(d2);
        if (a->type == 1)
            outSpecp->op += s / 2;
        else
            outSpecp->op += (s * 5) / 2;
    }
    if (ACSpecPartsP->insp) outSpecp->op += macspec_op_term(ACp, 10);
    struct PartsParamExtensionSt *e = ACSpecPartsP->extp;
    if (e && ((unsigned)(e->prog_no - 2) < 2 || e->prog_no == 4)) outSpecp->op += macspec_op_term(ACp, 12);
    struct struct_0ccde9cd *P = ACSpecPartsP;
    if (P->back_lp && !P->f_back_one) outSpecp->op += macspec_op_term(ACp, 15);
    P = ACSpecPartsP;
    if (!P->f_wback && P->back_rp) outSpecp->op += macspec_op_term(ACp, 14);
    if (ACSpecPartsP->gunp) outSpecp->op += macspec_op_term(ACp, 16);
    macspec_op_sword(outSpecp, ACp, ACSpecPartsP->swordp, 17);
    if (ACSpecPartsP->spgunp) outSpecp->op += macspec_op_term(ACp, 18);
    macspec_op_sword(outSpecp, ACp, ACSpecPartsP->spswordp, 19);
    outSpecp->op = outSpecp->op / 10;
}

/* every PartsParam*St starts with PartsParamBasicSt (wp 0xbc, ep 0xbe) */
static inline void macspec_add_wp(int *sum, const void *p)
{
    if (p) *sum += ((const struct PartsParamBasicSt *)p)->wp;
}

/* wp: total weight; legs_wp = wp without the legs (vs legs max_wp); core_wp = arms + inside + extension + gun +
 * sword (vs core max_wp). Steps 1-3 read pbp[], steps 4-5 the named pointers (Get's drop rules differ). */
static void __Calc_WP(struct struct_dbbd570b *ioSpecp)
{
    ioSpecp->wp = 0;
    for (int i = 0; i < 11; i++) {
        struct struct_0ccde9cd *P = ACSpecPartsP;
        struct PartsParamBasicSt *p = P->pbp[i];
        if (!p) continue;
        /* booster (pbp[4]) weight not counted for legs type 3 / 4; counted when there are no legs */
        if (i == 4 && P->legsp && (P->legsp->type == 3 || P->legsp->type == 4)) continue;
        ioSpecp->wp += p->wp;
    }
    struct struct_0ccde9cd *P = ACSpecPartsP;
    if (!P->f_wback && P->pbp[11] && !P->f_back_one) ioSpecp->wp += P->pbp[11]->wp;
    for (int i = 12; i < 16; i++)
        macspec_add_wp(&ioSpecp->wp, ACSpecPartsP->pbp[i]);

    ioSpecp->legs_wp = ioSpecp->wp;
    ioSpecp->legs_wp_max = 0;
    if (ACSpecPartsP->legsp) {
        ioSpecp->legs_wp -= ACSpecPartsP->legsp->basic.wp;
        ioSpecp->legs_wp_max = ACSpecPartsP->legsp->max_wp;
    }
    ioSpecp->core_wp = 0;
    ioSpecp->core_wp_max = 0;
    if (ACSpecPartsP->corep) ioSpecp->core_wp_max = ACSpecPartsP->corep->max_wp;
    macspec_add_wp(&ioSpecp->core_wp, ACSpecPartsP->armsp);
    macspec_add_wp(&ioSpecp->core_wp, ACSpecPartsP->insp);
    macspec_add_wp(&ioSpecp->core_wp, ACSpecPartsP->extp);
    macspec_add_wp(&ioSpecp->core_wp, ACSpecPartsP->gunp);
    macspec_add_wp(&ioSpecp->core_wp, ACSpecPartsP->swordp);
}

/* ep: energy drain of all parts (no sp weapons); ep_max / max_charge / effic_ep from the generator */
static void __Calc_EP(struct struct_dbbd570b *ioSpecp)
{
    ioSpecp->ep = 0;
    for (int i = 0; i < 11; i++)
        if (ACSpecPartsP->pbp[i]) ioSpecp->ep += ACSpecPartsP->pbp[i]->ep;
    struct struct_0ccde9cd *P = ACSpecPartsP;
    if (!P->f_wback && !P->f_back_one && P->pbp[11]) ioSpecp->ep += P->pbp[11]->ep;
    if (ACSpecPartsP->pbp[12]) ioSpecp->ep += ACSpecPartsP->pbp[12]->ep;
    if (ACSpecPartsP->pbp[13]) ioSpecp->ep += ACSpecPartsP->pbp[13]->ep;

    ioSpecp->ep_max = 0;
    if (ACSpecPartsP->genep) ioSpecp->ep_max = ACSpecPartsP->genep->power;
    ioSpecp->max_charge = 0;
    if (ACSpecPartsP->genep) {
        ioSpecp->max_charge = ACSpecPartsP->genep->charge_max;
        ioSpecp->max_charge += ACSpecPartsP->genep->charge_recov * 3;
        if (ACSpecPartsP->option & 8)
            ioSpecp->max_charge = (int)((float)ioSpecp->max_charge + g_asmparam.opt_cond_add);
    }
    ioSpecp->effic_ep = 0;
    if (ACSpecPartsP->genep) {
        int d = ioSpecp->ep_max - ioSpecp->ep;
        ioSpecp->effic_ep = ioSpecp->max_charge / 10 + d;
    }
}

static inline void macspec_dp_add(float &shell, float &energy, unsigned short s, unsigned short e)
{
    shell = shell + (float)(int)s;
    energy = energy + (float)(int)e;
}

/* dp = shell + energy defence of head / core / arms / legs, a shield sword (type 2), extension prog 9 / 6 */
static void __Calc_DP(struct struct_dbbd570b *ioSpecp)
{
    ioSpecp->dp = 0;
    float shell = 0.0f;
    float energy = 0.0f;
    struct struct_0ccde9cd *P = ACSpecPartsP;
    if (P->headp) macspec_dp_add(shell, energy, P->headp->defense.shell, P->headp->defense.energy);
    if (P->corep) macspec_dp_add(shell, energy, P->corep->defense.shell, P->corep->defense.energy);
    if (P->armsp) macspec_dp_add(shell, energy, P->armsp->defense.shell, P->armsp->defense.energy);
    if (P->legsp) macspec_dp_add(shell, energy, P->legsp->defense.shell, P->legsp->defense.energy);
    if (P->swordp && P->swordp->type == 2)
        macspec_dp_add(shell, energy, P->swordp->def_shell, P->swordp->def_energy);
    if (P->extp && (P->extp->prog_no == 9 || P->extp->prog_no == 6))
        macspec_dp_add(shell, energy, P->extp->def_shell, P->extp->def_energy);
    unsigned int o = P->option;
    if (o & 1) shell = shell * g_asmparam.opt_shelldef_add;
    if (o & 2) energy = energy * g_asmparam.opt_enedef_add;
    int s = (int)shell;
    int e = (int)energy;
    ioSpecp->dp = s + e;
    ioSpecp->def_energy = e;
    ioSpecp->def_shell = s;
}

/* the part's PartsParam record (_stMModel prm.part.infop, +0xd8) */
static inline struct PartsParamBasicSt *macspec_prm(struct _stMModel *m)
{
    return m ? (struct PartsParamBasicSt *)m->prm.part.infop : 0;
}

/* _stMModel::id read without a null test (PS2 address 0x98 when null) */
static inline int macspec_mmodel_id(struct _stMModel *m)
{
    if (!m) m = (struct _stMModel *)PS2_NULL_MEM();
    return m->id;
}

/* graph clamp of offence / defence / mobility */
static inline int macspec_graph_clamp(int v)
{
    if (v < 2) v = 1;
    if (v >= 100) v = 100;
    return v;
}

/* The AC spec of inACp (the temporary part of the connect screen too, unless inFlagTmp). Builds the ACSpecPartsP set
 * (pbp[] by equip index + named pointers; head / core / arms / booster / legs / radiator / generator are tuned copies
 * on this frame), runs the __Calc_* statics in their fixed order and computes the radar-graph values. */
void DF_MACSPEC_Get(struct struct_dbbd570b *outSpecp, struct stMenuAC *inACp, int inFlagTmp)
{
    PS2_FRAME(0x8c0);
    PS2_STACK(struct PartsParamLegsSt, legs, 0x40);
    PS2_STACK(struct PartsParamRadiatorSt, rad, 0x190);
    PS2_STACK(struct PartsParamGeneratorSt, gen, 0x2a0);
    PS2_STACK(struct PartsParamBoosterSt, boost, 0x3b0);
    PS2_STACK(struct PartsParamHeadSt, head, 0x4e0);
    PS2_STACK(struct PartsParamCoreSt, core, 0x620);
    PS2_STACK(struct PartsParamArmsSt, arms, 0x770);
    memset(&arms, 0, sizeof(arms));
    memset(&core, 0, sizeof(core));
    memset(&head, 0, sizeof(head));
    memset(&boost, 0, sizeof(boost));
    memset(&gen, 0, sizeof(gen));
    memset(&rad, 0, sizeof(rad));
    memset(&legs, 0, sizeof(legs));

    DF_MFUNC_ClearMemory(outSpecp, 0x27); /* words */
    if (!inACp->cur_part[1]) return;

    ACSpecPartsP = (struct struct_0ccde9cd *)DF_MMM_Alloc(2, 0x88);
    DF_MFUNC_ClearMemory(ACSpecPartsP, 0x22);
    struct struct_0ccde9cd *P = ACSpecPartsP;
    for (int i = 0; i < 16; i++) P->pbp[i] = 0;

    static const unsigned char slot[16] = {0, 1, 2, 5, 6, 7, 8, 9, 10, 12, 14, 15, 16, 17, 18, 19};
    for (int k = 0; k < 16; k++) P->pbp[k] = macspec_prm(inACp->cur_part[slot[k]]);
    P->option = DF_MPART_Option_Index2TrueID(inACp->option.equip[0]);

    /* temporary part of the connect screen; the equip index is not range-checked */
    if (inFlagTmp == 0 && inACp->flag.tmp_connect == 1) {
        int k = DF_M_AC_IDX2EquipIDX(inACp->connect.tmp_idx);
        P->pbp[k] = macspec_prm(inACp->real_part[(int)inACp->connect.tmp_idx]);
    }

    P->headp = (struct PartsParamHeadSt *)P->pbp[0];
    P->corep = (struct PartsParamCoreSt *)P->pbp[1];
    P->armsp = (struct PartsParamArmsSt *)P->pbp[2];
    P->legsp = (struct PartsParamLegsSt *)P->pbp[3];
    P->boostp = (struct PartsParamBoosterSt *)P->pbp[4];
    P->fcsp = (struct PartsParamFcsSt *)P->pbp[5];
    P->genep = (struct PartsParamGeneratorSt *)P->pbp[6];
    P->radp = (struct PartsParamRadiatorSt *)P->pbp[7];
    P->back_rp = (struct PartsParamBackSt *)P->pbp[10];
    P->back_lp = (struct PartsParamBackSt *)P->pbp[11];

    struct StAcAsmTune *tune = &inACp->cur_tune;
    if (P->headp) {
        head = *P->headp;
        GAcTune_TuneHead(&head, tune);
        P->headp = &head;
        P->pbp[0] = (struct PartsParamBasicSt *)&head;
    }
    if (P->corep) {
        core = *P->corep;
        GAcTune_TuneCore(&core, tune);
        P->corep = &core;
        P->pbp[1] = (struct PartsParamBasicSt *)&core;
    }
    if (P->armsp) {
        arms = *P->armsp;
        GAcTune_TuneArm(&arms, tune);
        P->armsp = &arms;
        P->pbp[2] = (struct PartsParamBasicSt *)&arms;
    }
    if (P->boostp) {
        boost = *P->boostp;
        GAcTune_TuneBooster(&boost, tune);
        P->boostp = &boost;
        P->pbp[4] = (struct PartsParamBasicSt *)&boost;
    }
    if (P->legsp) {
        legs = *P->legsp;
        GAcTune_TuneLeg(&legs, tune);
        P->legsp = &legs;
        P->pbp[3] = (struct PartsParamBasicSt *)&legs;
    }
    if (P->radp) {
        rad = *P->radp;
        GAcTune_TuneRadiator(&rad, tune);
        P->radp = &rad;
        P->pbp[7] = (struct PartsParamBasicSt *)&rad;
    }
    if (P->genep) {
        gen = *P->genep;
        GAcTune_TuneGenerator(&gen, tune);
        P->genep = &gen;
        P->pbp[6] = (struct PartsParamBasicSt *)&gen;
    }

    if (P->corep && P->corep->func_type == 1) P->f_ob = 1;

    /* drop rules: no arms -> no inside / extension / hand weapons; weapon arms -> no hand weapons */
    if (!P->armsp) {
        P->pbp[8] = 0;
        P->pbp[9] = 0;
        P->pbp[12] = 0;
        P->pbp[13] = 0;
        P->pbp[14] = 0;
        P->pbp[15] = 0;
    } else if (P->armsp->type != 0) {
        P->pbp[12] = 0;
        P->pbp[13] = 0;
        P->pbp[14] = 0;
        P->pbp[15] = 0;
    }
    if (!P->pbp[12]) P->pbp[14] = 0;
    if (!P->pbp[13]) P->pbp[15] = 0;
    if (!(P->corep && (P->corep->addon & 4))) {
        P->pbp[14] = 0;
        P->pbp[15] = 0;
    }
    if (P->pbp[14] && macspec_mmodel_id(inACp->cur_part[16]) == macspec_mmodel_id(inACp->cur_part[18])) P->pbp[14] = 0;
    if (P->pbp[15] && macspec_mmodel_id(inACp->cur_part[17]) == macspec_mmodel_id(inACp->cur_part[19])) P->pbp[15] = 0;

    P->insp = (struct PartsParamInsideSt *)P->pbp[8];
    P->extp = (struct PartsParamExtensionSt *)P->pbp[9];
    P->gunp = (struct PartsParamGunSt *)P->pbp[12];
    P->swordp = (struct PartsParamSwordSt *)P->pbp[13];
    P->spgunp = (struct PartsParamGunSt *)P->pbp[14];
    P->spswordp = (struct PartsParamSwordSt *)P->pbp[15];

    if (DF_MMODEL_IsWBackWeapon(inACp->cur_part[15]) == 1 || DF_MMODEL_IsWBackWeapon(inACp->cur_part[14]) == 1)
        P->f_wback = 1;

    outSpecp->ap = 0;
    if (P->headp) outSpecp->ap += P->headp->defense.ap;
    if (P->corep) outSpecp->ap += P->corep->defense.ap;
    if (P->armsp) outSpecp->ap += P->armsp->defense.ap;
    if (P->legsp) outSpecp->ap += P->legsp->defense.ap;

    __Calc_WP(outSpecp);
    __Calc_EP(outSpecp);
    __Calc_DP(outSpecp);
    __Calc_OP(outSpecp, inACp);
    __Calc_Price(outSpecp);
    __Calc_Stability(outSpecp);
    __Calc_Mobility(outSpecp);
    __Calc_Cooling(outSpecp);

    /* ECCM: head + FCS + back weapons (the right one not gated by f_wback) */
    P = ACSpecPartsP;
    int e = 0;
    if (P->headp) e += P->headp->radar_stat.eccm_power;
    if (P->fcsp) e += P->fcsp->eccm_power;
    if (P->back_rp) e += P->back_rp->radar_stat.eccm_power;
    if (!P->f_wback && !P->f_back_one && P->back_lp) e += P->back_lp->radar_stat.eccm_power;
    outSpecp->eccm_perform = e;
    if (ACSpecPartsP->option & 0x1000000)
        outSpecp->eccm_perform = (int)((float)outSpecp->eccm_perform + g_asmparam.opt_eccm_up);

    __Calc_LockonProper(outSpecp, inACp);
    __Calc_Support(outSpecp);

    outSpecp->surplus_ep = 0;
    if (ACSpecPartsP->genep) outSpecp->surplus_ep = ACSpecPartsP->genep->power - outSpecp->ep;
    __Calc_Grade(outSpecp);
    __Check_Info(outSpecp);

    if (ACSpecPartsP->corep) outSpecp->core_type = ACSpecPartsP->corep->func_type == 1 ? 0 : 1;

    /* radar graph (the divisors are main sdata floats 0x27b3d8..e4) */
    float q = (float)outSpecp->ap / 9999.0f;
    float f = 200.0f * q;
    f = f - 100.0f;
    int v = (int)f;
    if (v < 31) v = 30;
    if (v >= 100) v = 100;
    outSpecp->graph_ap = v;

    q = (float)outSpecp->op / 9000.0f;
    outSpecp->graph_offence = macspec_graph_clamp((int)(100.0f * q));
    q = (float)outSpecp->dp / 4000.0f;
    outSpecp->graph_defence = macspec_graph_clamp((int)(100.0f * q));

    P = ACSpecPartsP;
    float a = 0.0f;
    float b = 0.0f;
    if (P->legsp) {
        a = (float)(int)P->legsp->speed;
        b = (float)(int)P->legsp->rot_speed;
    }
    float ob = 0.0f;
    if (P->corep && P->f_ob) ob = (float)(int)P->corep->ob_power;
    float s = a + b;
    float o5 = ob / 5.0f;
    s = o5 + s;
    q = s / 1600.0f;
    outSpecp->graph_mobility = macspec_graph_clamp((int)(100.0f * q));

    __Calc_GraphAmmo(outSpecp, inACp);
    DF_MMM_Free(2, ACSpecPartsP); /* ACSpecPartsP is left dangling */
}
