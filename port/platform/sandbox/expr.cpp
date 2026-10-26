/* Sandbox expression evaluator (Stage 0: the part of the hybrid-era script engine, script.cpp, that the v3 replay
 * event player needs - events.cpp evaluates the place / value expressions of its poke / freeze lines through
 * sbx_eval). The script engine itself (--script authoring: commands, macros, watches, includes) is not in Stage 0;
 * the development version keeps it. Expressions: integers (dec / 0x hex), places (sandbox.h: NAME, FILE.c:NAME, @0xADDR,
 * NAME[i].field = the address in the running build, from the generated name table), `vb` (vblank), `ovl` (0 none,
 * 1 OLSORTIE, 2 OLMENU); loads u8() u16() u32() s8() s16() s32() f32() of an address; obj(FN) / scene(FN) = the live
 * OLMENU object / scene-wrapped screen running FN, else 0; operators ( ) ! ~ - * / % + - << >> & ^ | < <= > >= == !=
 * && ||. A load from an unmapped address is 0. Recorded event lines hold no macros (expanded at record time). */
#include "sandbox.h"
#include "sandbox_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

namespace {

const SbxBackend *&be = sbx_be;   /* the build backend (platform/names) */

void logf(const char *fmt, ...)
{
    char b[4600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    be->log(b);
}

/* ---------------------------------------------------------------- memory (backend) */

u8 rd8(u32 a) { const void *h = be->ptr(a, 1); return h ? *(const u8 *)h : 0; }
u32 rd32(u32 a)
{
    const void *h = be->ptr(a, 4);
    u32 v = 0;
    if (h) memcpy(&v, h, 4);
    return v;
}

/* ---------------------------------------------------------------- menu objects */

/* OLMENU's object array (menu/mobj.c): MenuObjTopp .. MenuObjEndp, 0x140-byte stMobj slots; byte 0 bit 0 f_use,
 * bit 1 f_erase; play_func @4. A screen is the object whose play_func is its Play / FadeIn / FadeOut function. */
u32 mobj_topp, mobj_endp, mobj_size = 0x140u;

u32 mobj_find(u32 fn)
{
    if (be->overlay() != 2 || !fn) return 0;
    u32 top = rd32(mobj_topp), end = rd32(mobj_endp);
    if (!be->ptr(top, 1) || end < top || (end - top) / mobj_size > 4096) return 0;
    for (u32 o = top; o < end; o += mobj_size)
        if ((rd8(o) & 3) == 1 && rd32(o + 4) == fn) return o;
    return 0;
}

/* Scene screens (top menu MTopRT etc.) are wrapped by MobjWrapScene_New (menu/mscene.c): the wrapper object takes
 * the screen's play_func into +0x20 (child +4 is zeroed) and keeps the screen object at +0x1c. -> the screen object. */
u32 scene_find(u32 fn)
{
    if (be->overlay() != 2 || !fn) return 0;
    u32 top = rd32(mobj_topp), end = rd32(mobj_endp);
    if (!be->ptr(top, 1) || end < top || (end - top) / mobj_size > 4096) return 0;
    for (u32 o = top; o < end; o += mobj_size)
        if ((rd8(o) & 3) == 1 && rd32(o + 0x20) == fn && be->ptr(rd32(o + 0x1c), 1)) return rd32(o + 0x1c);
    return 0;
}

/* ---------------------------------------------------------------- expressions */

/* lint = load-time check: names resolved, raw address literals refused, nothing read from memory */
/* fnarg: parsing obj() / scene()'s argument; absent: it named a function this build does not have (source port: not
 * native yet) - no live screen can run it, so obj() / scene() of it is 0 instead of an error */
struct Ex { const char *s; const char *err; int depth; int lint; int fnarg; int absent; int ovl; };   /* ovl: sbx_eval */
void ovl_merge(Ex *p, int o)   /* the overlay the named symbols need: one of them, or -1 for both */
{
    if (!o || p->ovl == o || p->ovl < 0) return;
    p->ovl = p->ovl || o < 0 ? -1 : o;
}
void ws(Ex *p) { while (*p->s == ' ' || *p->s == '\t') p->s++; }
int eat(Ex *p, const char *t)
{
    ws(p);
    size_t n = strlen(t);
    if (strncmp(p->s, t, n)) return 0;
    if (n == 1 && strchr("<>&|=!", t[0]) && p->s[1] && strchr("<>&|=", p->s[1])) return 0;   /* '<' must not eat "<=" */
    p->s += n;
    return 1;
}

double expr(Ex *p);

double load(const char *fn, u32 a)
{
    const u8 *h = (const u8 *)be->ptr(a, 4);
    if (!h) return 0;
    if (!strcmp(fn, "u8")) return *h;
    if (!strcmp(fn, "s8")) return (s8)*h;
    u16 v16; u32 v32; float f;
    if (!strcmp(fn, "u16")) { memcpy(&v16, h, 2); return v16; }
    if (!strcmp(fn, "s16")) { memcpy(&v16, h, 2); return (s16)v16; }
    if (!strcmp(fn, "u32")) { memcpy(&v32, h, 4); return v32; }
    if (!strcmp(fn, "s32")) { memcpy(&v32, h, 4); return (s32)v32; }
    if (!strcmp(fn, "f32")) { memcpy(&f, h, 4); return f; }
    return 0;
}

Ex *idx_ex;   /* the expression a field path's [index] belongs to */
long path_index(const char **s, const char **err)
{
    Ex q = {*s, 0, idx_ex->depth + 1, idx_ex->lint, 0, 0};
    Ex *save = idx_ex;
    double v = expr(&q);
    idx_ex = save;
    ovl_merge(save, q.ovl);
    ws(&q);
    *s = q.s;
    if (q.err) *err = q.err;
    return (long)(s64)v;
}

int is_id(int c) { return isalnum((unsigned char)c) || c == '_'; }

double primary(Ex *p)
{
    ws(p);
    if (p->err) return 0;
    if (eat(p, "(")) { double v = expr(p); if (!eat(p, ")")) p->err = "missing )"; return v; }
    if (*p->s == '@') {   /* @0xADDR: the symbol at that PS2 address */
        char *e;
        u32 ps2 = (u32)strtoull(p->s + 1, &e, 0);
        if (e == p->s + 1) { p->err = "@ needs a PS2 address"; return 0; }
        p->s = e;
        u32 a; const char *t, *err;
        if (!sbx_lookup_ps2(ps2, &a, &t, &err)) {
            if (p->fnarg && sbx_absent) {   /* as a name below */
                if (p->lint) logf("script: note: %s - obj() / scene() of it is 0 here", err);
                p->absent = 1;
                return 0;
            }
            p->err = err;
            return 0;
        }
        ovl_merge(p, sbx_last_ovl);
        idx_ex = p;
        if (!sbx_walk(&p->s, &a, &t, &err, path_index)) { p->err = err; return 0; }
        return a;
    }
    if (isdigit((unsigned char)*p->s)) {
        char *e; double v = (double)strtoull(p->s, &e, 0);
        if (p->lint && v >= 0x100000) {
            static char msg[200];
            snprintf(msg, sizeof msg, "raw address literal %.*s (use a symbol name, FILE.c:NAME or @0x..)", (int)(e - p->s), p->s);
            p->err = msg;
        }
        p->s = e;
        return v;
    }
    if (isalpha((unsigned char)*p->s) || *p->s == '_') {
        /* FILE.c:NAME / dir/FILE.c:NAME qualifier */
        const char *q = p->s, *qual = 0;
        size_t qn = 0;
        while (is_id(*q) || *q == '/') q++;
        if (*q == '.' && (!strncmp(q, ".c:", 3) || !strncmp(q, ".cpp:", 5))) {
            qual = p->s;
            q += q[2] == ':' ? 2 : 4;
            qn = (size_t)(q - p->s);
            p->s = q + 1;
        }
        char id[256]; int n = 0;
        while (is_id(*p->s) && n < 255) id[n++] = *p->s++;
        id[n] = 0;
        if (!qual) {
            static const char *fns[] = {"u8", "u16", "u32", "s8", "s16", "s32", "f32"};
            for (unsigned i = 0; i < sizeof fns / sizeof *fns; i++)
                if (!strcmp(id, fns[i])) {
                    if (!eat(p, "(")) { p->err = "load needs ( )"; return 0; }
                    int fa = p->fnarg;
                    p->fnarg = 0;
                    double a = expr(p);
                    p->fnarg = fa;
                    if (!eat(p, ")")) { p->err = "missing )"; return 0; }
                    return p->lint ? 0 : load(id, (u32)(s64)a);
                }
            if (!strcmp(id, "obj") || !strcmp(id, "scene")) {
                if (!eat(p, "(")) { p->err = "obj / scene need ( )"; return 0; }
                int fa = p->fnarg, ab = p->absent;
                p->fnarg = 1;
                p->absent = 0;
                double f = expr(p);
                int gone = p->absent;
                p->fnarg = fa;
                p->absent = ab;
                if (!eat(p, ")")) { p->err = "missing )"; return 0; }
                if (p->lint || gone) return 0;
                return id[0] == 'o' ? mobj_find((u32)(s64)f) : scene_find((u32)(s64)f);
            }
            if (!strcmp(id, "vb")) return p->lint ? 0 : be->vblank();
            if (!strcmp(id, "ovl")) return p->lint ? 0 : be->overlay();
        }
        u32 a; const char *t, *err;
        if (!sbx_lookup(qual, qn, id, &a, &t, &err)) {
            if (p->fnarg && sbx_absent) {   /* obj(FN) of a function this build lacks: 0, noted once at load */
                if (p->lint) logf("script: note: %s - obj() / scene() of it is 0 here", err);
                p->absent = 1;
                return 0;
            }
            static char msg[500];
            snprintf(msg, sizeof msg, "%s", err);
            p->err = msg;
            return 0;
        }
        ovl_merge(p, sbx_last_ovl);
        idx_ex = p;
        if (!sbx_walk(&p->s, &a, &t, &err, path_index)) { p->err = err; return 0; }
        return a;
    }
    p->err = "bad expression";
    return 0;
}

double unary(Ex *p)
{
    if (eat(p, "!")) return !unary(p);
    if (eat(p, "~")) return (double)(u32)~(u32)(s64)unary(p);
    if (eat(p, "-")) return -unary(p);
    return primary(p);
}
#define I(x) ((s64)(x))
double mul(Ex *p)
{
    double v = unary(p);
    for (;;) {
        if (eat(p, "*")) v *= unary(p);
        else if (eat(p, "/")) { double d = unary(p); v = d ? v / d : 0; }
        else if (eat(p, "%")) { s64 d = I(unary(p)); v = d ? (double)(I(v) % d) : 0; }
        else return v;
    }
}
double add(Ex *p)
{
    double v = mul(p);
    for (;;) { if (eat(p, "+")) v += mul(p); else if (eat(p, "-")) v -= mul(p); else return v; }
}
double shift(Ex *p)
{
    double v = add(p);
    for (;;) {
        if (eat(p, "<<")) v = (double)(I(v) << I(add(p)));
        else if (eat(p, ">>")) v = (double)(I(v) >> I(add(p)));
        else return v;
    }
}
double cmp(Ex *p)
{
    double v = shift(p);
    for (;;) {
        if (eat(p, "<=")) v = v <= shift(p);
        else if (eat(p, ">=")) v = v >= shift(p);
        else if (eat(p, "<")) v = v < shift(p);
        else if (eat(p, ">")) v = v > shift(p);
        else return v;
    }
}
double equ(Ex *p)
{
    double v = cmp(p);
    for (;;) { if (eat(p, "==")) v = v == cmp(p); else if (eat(p, "!=")) v = v != cmp(p); else return v; }
}
double band(Ex *p) { double v = equ(p); while (eat(p, "&")) v = (double)(I(v) & I(equ(p))); return v; }
double bxor(Ex *p) { double v = band(p); while (eat(p, "^")) v = (double)(I(v) ^ I(band(p))); return v; }
double bor(Ex *p) { double v = bxor(p); while (eat(p, "|")) v = (double)(I(v) | I(bxor(p))); return v; }
double land(Ex *p) { double v = bor(p); while (eat(p, "&&")) { double r = bor(p); v = v && r; } return v; }
double expr(Ex *p) { double v = land(p); while (eat(p, "||")) { double r = land(p); v = v || r; } return v; }

}  // namespace

int sbx_eval(const char *e, int lint, double *out, const char **err, int *ovl)
{
    Ex p = {e, 0, 0, lint, 0, 0, 0};
    *out = expr(&p);
    ws(&p);
    if (!p.err && *p.s) p.err = "junk after expression";
    if (ovl) *ovl = p.ovl;
    if (p.err) { static char m[900]; snprintf(m, sizeof m, "%s in \"%s\"", p.err, e); *err = m; return 0; }
    return 1;
}

int sbx_engine_init(const char **err)
{
    static int done;
    if (done) return 1;
    if (!be) { *err = "no sandbox backend"; return 0; }
    if (!sbx_names_load(be, err)) return 0;
    const char *t;
    if (!sbx_lookup(0, 0, "MenuObjTopp", &mobj_topp, &t, err) || !sbx_lookup(0, 0, "MenuObjEndp", &mobj_endp, &t, err)) return 0;
    u32 ms = sbx_type_size("s:stMobj");
    if (ms && ms != mobj_size) logf("script: stMobj is 0x%x bytes in the type table, the engine assumes 0x%x", ms, mobj_size);
    done = 1;
    return 1;
}

/* no script engine in Stage 0: these report the v3 replay's event state only */
extern "C" int sbx_script_active(void) { return 0; }
extern "C" int sbx_script_failed(void) { return sbx_ev_failed(); }
extern "C" int sbx_exit_code(void) { return sbx_ev_failed(); }
