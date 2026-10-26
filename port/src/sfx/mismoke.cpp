/* src/sfx/mismoke.cpp - smoke / lightning trails (magic type 3; logic part). Up to 96 trails in SmokeTop (handles =
 * slot + 1, 0 = none), each a ring of _smoke_pmax points (24; 8 with Init(reduce)) with pos / alpha / n_speed
 * buffers allocated once by DF_MAGIC3_Init. DF_MAGIC3_Proc (once per frame) fades and drifts every point, advances
 * the ring every MAGIC3_FRAME frames, appends the emitter's current position (a float4 or the translation row of an
 * ObjCMatSt) and queues the live trails in SmokeDraw for DF_MkDL_Magic3. Used by missile / rocket smoke (spell progs
 * 04 / 08). Draw part (phase 2 g9a): DF_MkDL_Magic3 appends the trails to the frame display list - smoke strips
 * (kinds 0 / 2 / 3: __mismoke_Draw -> __mismoke_DrawSmoke) after one GS state header, lightning ribbons (kind 1:
 * __mismoke_Draw2 -> __mismoke_DrawLightning) after a second one with an animated texture. Points are staged in the
 * scratchpad, projected with VU0 macro code (the live VU0() registers, incl. the stale vf12.w / vf13.w fog factors
 * __MakeSfxDl left) and written as one DIRECT GIF strip packet per pass. */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"
#include "acnb/vu0m.h"

/* Clears every trail and its buffers. The buffers are not checked: after a failed Init a null one is cleared from
 * address 0 (never in Versus). */
void Mismoke_AllDelete(void)
{
    for (int i = 0; i < 0x60; i++) {
        struct_6f39c65b *p = &SmokeTop[i];
        p->use = 0;
        ZeroMemory32b(p->pos, (unsigned int)(_smoke_pmax << 4) >> 2);
        ZeroMemory32b(p->alpha, (unsigned int)_smoke_pmax);
        ZeroMemory32b(p->n_speed, (unsigned int)(_smoke_pmax << 4) >> 2);
        SmokeDraw[i] = 0;
    }
    n_SmokeDraw = 0;
    n_smoke = 0x60;
    Smoke = 0;
}

void DF_MAGIC3_Kill(int id)
{
    int i = id - 1;
    if (i < 0 || i >= 0x60 || SmokeTop[i].use == 0) return;
    SmokeTop[i].use = 2;
}

/* One frame of one trail. use 2 (killed) trails keep fading and shifting but get no new points; the slot is freed
 * when a shift finds no visible point left. */
static void __mismoke_Main(struct_6f39c65b *mp)
{
    static const float drift[4] = { -0.0075f, -0.0f, 0.0f, 1.0f };   /* 0x260170, added to every visible point */
    float (*pos)[4] = mp->pos;
    void *point = mp->point;
    float *alpha = mp->alpha;
    float (*n_speed)[4] = mp->n_speed;
    float (*velocity)[4] = mp->velocity;
    int cnt = 0;

    if (!pos || !alpha) {
        mp->use = 0;
        n_smoke++;
        return;
    }

    float decay;
    if (mp->sw.kind == 0) {
        MAGIC3_FRAME = 5;
        decay = 0.95f;
    } else {
        decay = 0.85f;
        MAGIC3_FRAME = 2;
    }

    for (int i = 0; i < _smoke_pmax; i++) {
        alpha[i] = alpha[i] * decay;
        if (mp->sw.kind == 1 || alpha[i] < 1.0f) continue;
        /* two separate vadd.xyz + sqc2 (w kept) */
        for (int k = 0; k < 3; k++) pos[i][k] = pos[i][k] + n_speed[i][k];
        for (int k = 0; k < 3; k++) pos[i][k] = pos[i][k] + drift[k];
    }

    int time = mp->time;
    if (ps2_mod32(time, MAGIC3_FRAME) == 0 || time < MAGIC3_FRAME) {
        int j;
        for (j = 0; j < _smoke_pmax - 1; j++) {
            if (alpha[j + 1] < 1.0f) {
                alpha[j] = 0.0f;
            } else {
                memcpy(pos[j], pos[j + 1], 16);
                cnt++;
                alpha[j] = alpha[j + 1];
            }
        }
        alpha[j] = 0.0f;
        if (mp->use != 1 && cnt == 0) {
            mp->use = 0;
            n_smoke++;
            return;
        }
    }

    int last = _smoke_pmax - 1;
    if (mp->use == 1) {
        if (mp->sw.pointer == 1) {
            float tmp[4][4];
            ObjCMatSt *ocmp = (ObjCMatSt *)point;
            if (ocmp && ocmp->parentmatp) {
                vu_mul_matrix(tmp, (const float (*)[4])*ocmp->parentmatp, (const float (*)[4])ocmp->mat);
            } else {
                /* sceVu0CopyMatrix(tmp, point): with point == NULL the original copies from address 0 */
                memcpy(tmp, ocmp ? (const void *)ocmp : PS2_NULL_MEM(), sizeof tmp);
            }
            memcpy(pos[last], tmp[3], 16);
        } else {
            memcpy(pos[last], point, 16);
        }
        pos[last][3] = 1.0f;
        alpha[last] = 64.0f;
        if (mp->sw.kind == 1) alpha[last] = alpha[last] * 4.0f;
        if (velocity) {
            /* vmulx.xyz + sqc2: n_speed.w = velocity.w */
            float v[4];
            memcpy(v, *velocity, 16);
            for (int k = 0; k < 3; k++) v[k] = v[k] * -0.5f;
            memcpy(n_speed[last], v, 16);
        }
    }
    mp->time++;
}

void DF_MAGIC3_Proc(void)
{
    n_SmokeDraw = 0;
    for (int i = 0; i < 0x60; i++) {
        struct_6f39c65b *p = &SmokeTop[i];
        if (!p->use) continue;
        __mismoke_Main(p);
        if (p->use) SmokeDraw[n_SmokeDraw++] = p;
    }
}

static int __mismoke_CheckFree(void *adr)
{
    return ((struct_6f39c65b *)adr)->use == 0;
}

/* Starts a trail following `point` (a float4) or, when point is NULL, the translation of `ocmp`. velocity (may be
 * NULL) gives the newest point's drift (-velocity / 2). Returns the handle (slot + 1) or 0. DF_GetFreeBuffer's
 * bound is 0x5f, so slot 95 is never used. */
unsigned char DF_MAGIC3_Appear(float (*point)[4], ObjCMatSt *ocmp, float (*velocity)[4], unsigned char kind)
{
    PS2_FRAME(0x60);
    int idx = 0;
    if (n_smoke == 0 || (!point && !ocmp)) return 0;
    struct_6f39c65b *p =
        (struct_6f39c65b *)DF_GetFreeBuffer(&Smoke, SmokeTop, 0x20, 0x5f, GFN(__mismoke_CheckFree));
    if (!p) return 0;
    idx = Smoke;
    Smoke = idx + 1;
    if (Smoke >= 0x60) Smoke = 0;

    n_smoke--;
    p->use = 1;
    p->sw.kind = kind & 3;
    p->time = 0;
    if (point) {
        p->sw.pointer = 0;
        p->point = point;
    } else {
        p->sw.pointer = 1;
        p->point = ocmp;
    }
    p->velocity = velocity;
    if (velocity) {
        float v[4];
        memcpy(v, *velocity, 16);
        for (int k = 0; k < 3; k++) v[k] = v[k] * -0.5f;
        memcpy(p->n_speed[_smoke_pmax - 1], v, 16);
    }
    return (unsigned char)(idx + 1);
}

/* Allocates every trail's buffers (Memm pool 1). Stops at the first failed allocation, leaving the rest
 * unallocated; the previous buffers are not freed (the pool is reset elsewhere). */
void DF_MAGIC3_Init(int reduce)
{
    PS2_FRAME(0x30);
    n_smoke = 0x60;
    ZeroMemory32b(SmokeTop, 0x300);
    Smoke = 0;
    _smoke_pmax = 24;
    MAGIC3_FRAME = 5;
    if (reduce) {
        MAGIC3_FRAME = 8;
        _smoke_pmax = _smoke_pmax / 3;
    }
    for (int i = 0; i < 0x60; i++) {
        struct_6f39c65b *p = &SmokeTop[i];
        p->pos = (float (*)[4])Memm_Alloc(1, _smoke_pmax << 4, 0x10);
        p->alpha = (float *)Memm_Alloc(1, _smoke_pmax << 2, 0x10);
        p->n_speed = (float (*)[4])Memm_Alloc(1, _smoke_pmax << 4, 0x10);
        if (!p->pos || !p->alpha || !p->n_speed) return;
        ZeroMemory32b(p->pos, (unsigned int)(_smoke_pmax << 4) >> 2);
        ZeroMemory32b(p->alpha, (unsigned int)_smoke_pmax);
        ZeroMemory32b(p->n_speed, (unsigned int)(_smoke_pmax << 4) >> 2);
    }
}

/* ---------------------------------------------------------------- draw path (phase 2 g9a) */

#define MSM_PI     0x1.921fb6p+1f   /* 0x40490fdb */
#define MSM_2PI    0x1.921fb6p+2f   /* 0x40c90fdb */
#define MSM_HALFPI 0x1.921fb6p+0f   /* 0x3fc90fdb */

/* vf16 = M * vf16 with M = vf1..vf4: vmulax / vmadday / vmaddaz.xyzw ACC, vmaddw.xyzw vf16 */
static void mismoke_vu_apply16(void)
{
    auto *v = VU0();
    float x = v->vf[16].f[0], y = v->vf[16].f[1], z = v->vf[16].f[2], w = v->vf[16].f[3];
    for (int i = 0; i < 4; i++) {
        float a = v->vf[1].f[i] * x;
        a = a + v->vf[2].f[i] * y;
        a = a + v->vf[3].f[i] * z;
        v->acc[i] = a;
        v->vf[16].f[i] = a + v->vf[4].f[i] * w;
    }
}

/* lqc2 vf16, src; projection through vf1..vf4; vf26.xyz = vf16 / w, vf26.w = vf12.w + vf13.w / w (the fog factors
 * __MakeSfxDl left in VU0: stale-register coupling as in weth.cpp); sqc2 ftoi4(vf26) -> dst; then the F word is
 * clamped to 0..0xff0 in memory (vf16 keeps the unclamped words). */
static void mismoke_project(const void *src, int *dst)
{
    auto *v = VU0();
    memcpy(v->vf[16].f, src, 16);
    mismoke_vu_apply16();
    v->q = ps2_div(1.0f, v->vf[16].f[3]);                   /* vdiv q, vf0w, vf16w */
    v->acc[3] = v->vf[12].f[3] * 1.0f;                      /* vmula.w acc, vf12, vf0 */
    v->vf[26].f[3] = v->acc[3] + v->vf[13].f[3] * v->q;     /* vmaddq.w vf26, vf13, q */
    for (int i = 0; i < 3; i++) v->vf[26].f[i] = v->vf[16].f[i] * v->q;   /* vmulq.xyz */
    for (int i = 0; i < 4; i++) v->vf[16].sw[i] = ps2_ftoi(v->vf[26].f[i] * 16.0f);   /* vftoi4 */
    memcpy(dst, v->vf[16].sw, 16);
    if (dst[3] < 0) dst[3] = 0;
    if (dst[3] >= 0xff1) dst[3] = 0xff0;
}

static void mismoke_load_wsm(void)
{
    auto *v = VU0();
    for (int k = 0; k < 4; k++) memcpy(v->vf[1 + k].f, WORLD_SCREEN_MATRIX[k], 16);
}

/* GS guard band / depth / alpha test of a strip vertex: out -> ADC on it and the next two */
static int mismoke_out(const int *s, int a)
{
    return s[0] < 0x4000 || s[0] >= 0xc001 || s[1] < 0x4000 || s[1] >= 0xc001 || s[2] <= 0 || s[2] >= 0x10000001
           || a <= 0;
}

/* Lightning strip of `all` vertices staged by __mismoke_Draw2 at SPR 0 (float alphas at SPR + (pmax-1)*0x80).
 * Per vertex: projection to the screen words (SPR + (pmax-1)*0x94, then + 16 - (addr & 15): +16 when aligned), byte
 * alpha ftoi(a * F) / 4080 into the table at SPR + (pmax-1)*0x90. The alpha pointer advances after EVEN vertices
 * (vertex k uses alpha[(k + 1) / 2]: pairs (1,2), (3,4) share one). For long strips the byte table runs into the screen
 * words (both live in SPR and are read back from it, as the original does). Packet: DIRECT, CLAMP_1 = 5 by A+D, then a
 * TRISTRIP (0x35c) of RGBAQ {0x60, 0xa0, 0x8c, abyte}, UV from the 8-entry corner table (case 2 v = 0, the others 8:
 * original typo), XYZF2 (ADC when this vertex or one of the two before is out). */
static unsigned int *__mismoke_DrawLightning(unsigned int *gp, int all)
{
    PS2_FRAME(0x80);
    static const unsigned short uvtab[8][2] = {{8, 8}, {8, 0x1f8}, {0x3f8, 0}, {0x3f8, 0x1f8},
                                               {8, 0x208}, {8, 0x3f8}, {0x3f8, 0x208}, {0x3f8, 0x3f8}};
    char *spr = (char *)PS2_SPR(0);
    int t = _smoke_pmax - 1;
    float *a = (float *)(spr + t * 0x80);
    unsigned char *ab0 = (unsigned char *)(spr + t * 0x90);
    int off = t * 0x94;
    off += 0x10 - (off & 0xf);
    int *scr0 = (int *)(spr + off);
    unsigned char flag[2] = {0, 0};   /* sp+0x7c (vertex k-2 out), sp+0x7d (vertex k-1 out) */

    mismoke_load_wsm();
    int *s = scr0;
    unsigned char *ab = ab0;
    const char *src = spr;
    for (int i = 0; i < all; i++, s += 4, src += 16, ab++) {
        mismoke_project(src, s);
        int c = ps2_ftoi(*a * (float)s[3]);
        *ab = (unsigned char)(c / 4080);
        if (i % 2 == 0) a++;
    }

    int n3 = all * 3;
    gp = MakeDMA(gp, n3 + 4, 1);
    gp = MakePACKET(gp, 0, 0, 0, 0x51000000u | (unsigned int)(n3 + 3));
    gp = MakeGIF(gp, 1, 1, 0, 0, 1, 0xe);
    ((unsigned long long *)gp)[0] = 5;   /* CLAMP_1 */
    ((unsigned long long *)gp)[1] = 8;
    gp += 4;
    gp = MakeGIF(gp, all, 1, 0x35c, 0, 3, 0x431);
    s = scr0;
    ab = ab0;
    for (int i = 0; i < all; i++, s += 4, ab++) {
        gp[0] = 0x60;
        gp[1] = 0xa0;
        gp[2] = 0x8c;
        gp[3] = *ab;
        gp += 4;
        int k = i % 8;
        gp = MakePACKET(gp, uvtab[k][0], uvtab[k][1], 0, 0);
        gp = MakePACKET(gp, s[0], s[1], s[2], s[3]);
        if (flag[0] || flag[1]) gp[-1] |= 0x8000;
        flag[0] = flag[1];
        if (mismoke_out(s, *ab)) {
            gp[-1] |= 0x8000;
            flag[1] = 1;
        } else {
            flag[1] = 0;
        }
    }
    return gp;
}

/* Lightning ribbon (kind 1): per live segment (both alphas >= 1; NaN counts as live) eight vertices at SPR 0 + 0x80*k
 * offset +-3.6 across the heading of the segment (Sintable) and +-0.5 in y, the two middle pairs at the midpoint,
 * with alphas {a[i], m, m, a[i+1]} (m = a[i] + (a[i+1] - a[i]) / 2); drawn, then only the y words are rewritten as a
 * vertical ribbon and drawn again. */
static unsigned int *__mismoke_Draw2(struct_6f39c65b *mp, unsigned int *gp)
{
    PS2_FRAME(0xb0);
    char *spr = (char *)PS2_SPR(0);
    float *alpha = mp->alpha;
    float (*pos)[4] = mp->pos;
    float *sa = (float *)(spr + (_smoke_pmax - 1) * 0x80);
    float *v = (float *)spr;
    const float one = 1.0f;
    int n = 0;
    for (int i = 0; i < _smoke_pmax - 1; i++) {
        if (alpha[i] < 1.0f) continue;
        if (alpha[i + 1] < 1.0f) continue;
        n++;
        float *p0 = pos[i], *p1 = pos[i + 1];
        float dx = p1[0] - p0[0];
        float dz = p1[2] - p0[2];
        float dy = p1[1] - p0[1];
        float ang = MSM_HALFPI + ps2_atan2f(dx, dz);
        while (!(ang <= MSM_PI)) ang -= MSM_2PI;
        while (ang < -MSM_PI) ang += MSM_2PI;
        int idx = (ps2_ftoi(ps2_div(4096.0f * ang, MSM_2PI)) + 0x1000) & 0xfff;
        float s = Sintable[idx], c = Sintable[idx + 0x400];
        dx = dx * 0.5f;
        float os = 3.6f * s, oc = 3.6f * c;
        dy = dy * 0.5f;
        dz = dz * 0.5f;
        v[0] = -os + p0[0];
        v[1] = 0.5f + p0[1];
        v[2] = -oc + p0[2];
        v[3] = one;
        v[4] = os + p0[0];
        v[5] = -0.5f + p0[1];
        v[6] = oc + p0[2];
        v[7] = one;
        sa[0] = alpha[i];
        v += 8;
        for (int r = 0; r < 2; r++) {
            v[0] = (-os + p0[0]) + dx;
            v[1] = (0.5f + p0[1]) + dy;
            v[2] = (-oc + p0[2]) + dz;
            v[3] = one;
            v[4] = (os + p0[0]) + dx;
            v[5] = (-0.5f + p0[1]) + dy;
            v[6] = (oc + p0[2]) + dz;
            v[7] = one;
            v += 8;
            if (r == 0) {
                sa[1] = alpha[i] + ps2_div(alpha[i + 1] - alpha[i], 2.0f);
                sa += 2;
            } else {
                sa[0] = sa[-1];
            }
        }
        v[0] = -os + p1[0];
        v[1] = 0.5f + p1[1];
        v[2] = -oc + p1[2];
        v[3] = one;
        v[4] = os + p1[0];
        v[5] = -0.5f + p1[1];
        v[6] = oc + p1[2];
        v[7] = one;
        sa[1] = alpha[i + 1];
        v += 8;
        sa += 2;
    }
    if (n == 0) return gp;
    gp = __mismoke_DrawLightning(gp, n * 8);

    v = (float *)spr;
    int n2 = 0;
    for (int i = 0; i < _smoke_pmax - 1; i++) {
        if (alpha[i] < 1.0f) continue;
        if (alpha[i + 1] < 1.0f) continue;
        n2++;
        float y0 = pos[i][1], y1 = pos[i + 1][1];
        float h = (y1 - y0) * 0.5f;
        v[1] = -0.5f + y0;
        v[5] = 0.5f + y0;
        v += 8;
        for (int r = 0; r < 2; r++, v += 8) {
            v[1] = (-0.5f + y0) + h;
            v[5] = (0.5f + y0) + h;
        }
        v[1] = -0.5f + y1;
        v[5] = 0.5f + y1;
        v += 8;
    }
    return __mismoke_DrawLightning(gp, n2 * 8);
}

/* Smoke strip: posr[k] / posl[k] alternate as vertices 2k / 2k+1 (screen words at SPR 0x600 + 16 * vertex, alpha
 * words ftoi(alpha[k]) * F / 4080 at SPR 0xc00 + 4 * vertex). Under 4 vertices nothing is drawn. Packet: DIRECT,
 * CLAMP_1 = 5 by A+D, TRISTRIP (0x35c) of RGBAQ {0x80, 0x80, 0x80, aword}, UV by vertex & 3 over a 1024 x 512 texel
 * quad, XYZF2 (ADC when this vertex or one of the two before is out). */
static unsigned int *__mismoke_DrawSmoke(unsigned int *gp, float (*posr)[4], float (*posl)[4], float *alpha, int all)
{
    PS2_FRAME(0x50);
    char *spr = (char *)PS2_SPR(0);
    mismoke_load_wsm();
    int n = 0;
    for (int k = 0; k < all; k++) {
        int *sr = (int *)(spr + 0x600 + 0x20 * k);
        int *aw = (int *)(spr + 0xc00 + 8 * k);
        mismoke_project(posr[k], sr);
        aw[0] = (int)((unsigned int)ps2_ftoi(alpha[k]) * (unsigned int)sr[3]) / 4080;
        int *sl = (int *)(spr + 0x600 + 16 * (n + 1));
        mismoke_project(posl[k], sl);
        aw[1] = (int)((unsigned int)ps2_ftoi(alpha[k]) * (unsigned int)sl[3]) / 4080;
        n += 2;
    }
    if (n < 4) return gp;

    gp = MakeDMA(gp, 3 * n + 4, 1);
    gp = MakePACKET(gp, 0, 0, 0, 0x51000000u | (unsigned int)(3 * n + 3));
    unsigned long long *t = (unsigned long long *)gp;   /* ZeroMemory32b + MWCC bitfield stores, final words */
    t[0] = 0x8001ull | 1ull << 60;                      /* NLOOP 1, EOP, NREG 1 */
    t[1] = 0xe;                                         /* A+D */
    t[2] = 5;                                           /* CLAMP_1 */
    t[3] = 8;
    t[4] = (unsigned long long)(n & 0x7fff) | 0x8000ull | 1ull << 46 | 0x35cull << 47 | 3ull << 60;
    t[5] = 0x431;                                       /* XYZF2, UV, RGBAQ */
    unsigned int *q = gp + 12;
    const int *sc = (const int *)(spr + 0x600);
    const int *aw = (const int *)(spr + 0xc00);
    int flag[2] = {0, 0};   /* sp+0x48 (vertex k-2 out), sp+0x4c (vertex k-1 out) */
    for (int i = 0; i < n; i++, sc += 4, aw++, q += 12) {
        q[0] = 0x80;
        q[1] = 0x80;
        q[2] = 0x80;
        q[3] = *aw;
        int k = i % 4;
        q[4] = (k & 2) ? 0x400 : 0;
        q[5] = (k & 1) ? 0x200 : 0;
        q[6] = 0;
        q[7] = 0;
        q[8] = sc[0];
        q[9] = sc[1];
        q[10] = sc[2];
        q[11] = sc[3];
        if (flag[0] || flag[1]) q[11] |= 0x8000;
        flag[0] = flag[1];
        if (mismoke_out(sc, *aw)) {
            q[11] |= 0x8000;
            flag[1] = 1;
        } else {
            flag[1] = 0;
        }
    }
    return q;
}

/* Smoke trail (kinds 0 / 2 / 3): per live segment a left / right point pair across the heading of the segment, half
 * width scale / alpha (kind 0: 32 / alpha; kinds 2 / 3: 16 / alpha halved), +-0.5 in y, plus the last point with
 * the last offsets (0 when no segment was live); drawn as a horizontal strip, then only y is rewritten (vertical
 * strip) and drawn again. The heading vector `ang` is wrapped in all three lanes (lanes 0 / 2 are always 0). */
static unsigned int *__mismoke_Draw(struct_6f39c65b *mp, unsigned int *gp)
{
    PS2_FRAME(0xf0);
    char *spr = (char *)PS2_SPR(0);
    float (*left)[4] = (float (*)[4])spr;
    float (*right)[4] = (float (*)[4])(spr + 0x300);
    float (*pos)[4] = mp->pos;
    float ang[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float os = 0.0f, oc = 0.0f;
    float half, scale;
    if (mp->sw.kind == 0) {
        half = 0.5f;
        scale = 32.0f;
    } else {
        half = 0.5f;
        scale = 16.0f;
    }
    int cnt = 0;
    int i;
    for (i = 0; i < _smoke_pmax - 1; i++) {
        float *alpha = mp->alpha;
        if (alpha[i] < 1.0f) continue;
        if (alpha[i + 1] < 1.0f) continue;
        __mismoke_Draw__alpha[cnt] = alpha[i];
        ang[1] = MSM_HALFPI + ps2_atan2f(pos[i + 1][0] - pos[i][0], pos[i + 1][2] - pos[i][2]);
        for (int k = 0; k < 3; k++) {
            while (!(ang[k] <= MSM_PI)) ang[k] -= MSM_2PI;
            while (ang[k] < -MSM_PI) ang[k] += MSM_2PI;
        }
        int idx = (ps2_ftoi(ps2_div(4096.0f * ang[1], MSM_2PI)) + 0x1000) & 0xfff;
        float s = Sintable[idx], c = Sintable[idx + 0x400];
        float k = ps2_div(scale, mp->alpha[i]);
        if (mp->sw.kind == 0) {
            os = k * s;
            oc = k * c;
        } else {
            os = 0.5f * (k * s);
            oc = 0.5f * (k * c);
        }
        left[cnt][0] = -os + pos[i][0];
        left[cnt][1] = half + pos[i][1];
        left[cnt][2] = -oc + pos[i][2];
        left[cnt][3] = 1.0f;
        right[cnt][0] = os + pos[i][0];
        right[cnt][1] = -half + pos[i][1];
        right[cnt][2] = oc + pos[i][2];
        right[cnt][3] = 1.0f;
        cnt++;
    }
    if (!(mp->alpha[i] < 1.0f)) {
        __mismoke_Draw__alpha[cnt] = mp->alpha[i];
        left[cnt][0] = -os + pos[i][0];
        left[cnt][1] = half + pos[i][1];
        left[cnt][2] = -oc + pos[i][2];
        left[cnt][3] = 1.0f;
        right[cnt][0] = os + pos[i][0];
        right[cnt][1] = -half + pos[i][1];
        right[cnt][2] = oc + pos[i][2];
        right[cnt][3] = 1.0f;
        cnt++;
    }
    if (cnt < 2) return gp;
    gp = __mismoke_DrawSmoke(gp, right, left, __mismoke_Draw__alpha, cnt);

    int n2 = 0;
    for (i = 0; i < _smoke_pmax - 1; i++) {
        float *alpha = mp->alpha;
        if (alpha[i] < 1.0f) continue;
        if (alpha[i + 1] < 1.0f) continue;
        left[n2][1] = -half + pos[i][1];
        right[n2][1] = half + pos[i][1];
        n2++;
    }
    if (!(mp->alpha[i] < 1.0f)) {
        left[n2][1] = -half + pos[i][1];
        right[n2][1] = half + pos[i][1];
        n2++;
    }
    return __mismoke_DrawSmoke(gp, right, left, __mismoke_Draw__alpha, n2);
}

/* Appends the trails to the frame display list: GS state header 1 (TEX1_2, TEX0_2 smoke texture, ALPHA_2, TEST_2)
 * + the smoke strips, header 2 (TEX1_2, TEX0_2 with TBP0 animated by FRAME & 31, ALPHA_2) + the lightning ribbons.
 * The rollback compares with the end of header 1 after header 2 was appended, so it never fires (both headers, 11
 * qwords, go out every frame: original quirk). Render tags: EFFECT, RQ_OC_EFFECT / 0xe4 << 16 | 0xffff for the
 * headers, 0xe4 << 16 | SmokeTop slot (sub = kind) per trail. */
void DF_MkDL_Magic3(unsigned int **gpp)
{
    PS2_FRAME(0x60);
    unsigned int *start = *gpp;
    ps2_rq_tag_packet(start, RQ_K_EFFECT, RQ_OBJID(RQ_OC_EFFECT, 0xe4u << 16 | 0xffffu), 0);
    unsigned int *g = MakeDMA(start, 6, 1);
    g = MakePACKET(g, 0, 0, 0, 0x51000005);
    g = MakeGIF(g, 4, 0, 0, 0, 1, 0xe);
    g = MakeREGS(g, 0x15, 0x60);
    g = MakeREGS(g, 7, 0x8007d4055930b878ull);
    g = MakeREGS(g, 0x43, 0x44);
    g = MakeREGS(g, 0x48, 0x71001);
    unsigned int *mid = g;
    for (int i = 0; i < n_SmokeDraw; i++) {
        struct_6f39c65b *p = SmokeDraw[i];
        if (p->sw.kind == 1) continue;
        ps2_rq_tag_packet(g, RQ_K_EFFECT, RQ_OBJID(RQ_OC_EFFECT, 0xe4u << 16 | (unsigned int)(p - SmokeTop)), p->sw.kind);
        g = __mismoke_Draw(p, g);
    }
    ps2_rq_tag_packet(g, RQ_K_EFFECT, RQ_OBJID(RQ_OC_EFFECT, 0xe4u << 16 | 0xffffu), 0);
    g = MakeDMA(g, 5, 1);
    g = MakePACKET(g, 0, 0, 0, 0x51000004);
    g = MakeGIF(g, 1, 0, 0, 0, 3, 0xeee);
    g = MakeREGS(g, 0x15, 0x60);
    g = MakeREGS(g, 7, 0x8007db2599408000ull | (unsigned long long)(((FRAME & 0x1f) << 3) + 0x3140));
    g = MakeREGS(g, 0x43, 0x48);
    for (int i = 0; i < n_SmokeDraw; i++) {
        struct_6f39c65b *p = SmokeDraw[i];
        if (p->sw.kind != 1) continue;
        ps2_rq_tag_packet(g, RQ_K_EFFECT, RQ_OBJID(RQ_OC_EFFECT, 0xe4u << 16 | (unsigned int)(p - SmokeTop)), 1);
        g = __mismoke_Draw2(p, g);
    }
    *gpp = g == mid ? start : g;
    ps2_rq_tag_packet(*gpp, RQ_K_NONE, 0, 0);
}
