/* Logical state hash engine. Field file -> flat program at load (per area: items,
 * each = root address + pointer-deref chain + leaves with offset / kind), then per vblank: pools, gates, leaves fed in
 * list order as 64-bit words (ints sign / zero extended, bitfields extracted, floats as raw bits, pointers as the hash
 * of their identity string), one 64-bit multiply-xorshift hash per area, the core total over the gated areas and the
 * full total (core total + every area's full hash).
 *
 * Field file (port/statehash/fields.txt; '#' = comment to end of line):
 *   gate NAME COND [&& COND ..]           a named predicate (usable in later conds)
 *   pool NAME PATH [type=S] [n=OPND] [unordered] [if COND ..]
 *                                         a pointer pool (rule 2): PATH = a static array (element type + count from
 *                                         DWARF; n= overrides the count) or a pointer global (type=S element struct and
 *                                         n= count required; the pointer is read every vblank, 0 = pool off). A
 *                                         pointer inside it hashes as "NAME:INDEX.member.path" (pool order = rule order);
 *                                         `unordered` pools (allocator slots) hash as "NAME:*.member" in the core tier
 *   member STRUCT.MEMBER keep|skip|ptr|words|nN
 *                                         struct-expansion override: keep (a real member the padding filter would drop),
 *                                         skip (never expanded), ptr (a u32 / s32 that holds a pointer), words (the
 *                                         member as 32-bit words, identity where a word resolves - a raw byte buffer
 *                                         that holds structs with pointers; a data word in a symbol's range gives
 *                                         a false identity, so prefer typed items), nN (an array member:
 *                                         only its first N elements - a vec4 whose w is stale stack, stCom.move_loc)
 *   heap SYMBOL                           a data symbol that is the game's heap (Memm s_bufGeneral): a pointer into
 *                                         it hashes as "H" in the core tier (rule 5: no heap offsets in the parity
 *                                         contract) and as "SYMBOL+0xOFF" in the full tier (allocator drift while
 *                                         the layouts are equal)
 *   alias TABLE K NAME                    the 32-bit function pointer TABLE[K] hashes as NAME (source port lambda
 *                                         adaptors: com_extinit_tbl slots hold `$_N::__invoke` thunks around a
 *                                         void(void) PS2 function; the hybrid's slot is NAME itself)
 *   area NAME [xN] [each POOL [from K] [ordered]] [if COND [&& COND ..]]
 *                                         starts an area; xN = N copies with $i = 0..N-1 in the name, conds and items
 *                                         (area ac$i x4 -> ac0..ac3); no `if` = always on. `each POOL` = one pass per
 *                                         pool element K.. (items starting with '.', '[' or '->' are relative to the
 *                                         element; `.` alone = the whole element): core = order-free (wrapping sum of
 *                                         the per-element hashes + the live count, token "N:HASH"), full = slot order;
 *                                         `ordered` = the core tier keeps slot order too (menu mobj slots: the slot
 *                                         allocator is part of the menu's logic, children after parents)
 *   live COND [&& COND ..]                (each-areas) the element is hashed only when true (relative operands)
 *   PATH [type=T] [n=OPND] [at=OPND] [when=C[,C..]]
 *                                         one core item (a struct / array = all its leaves); [A..B] in a path = one
 *                                         item per index; n= / at= on an array item = only elements at..at+n-1;
 *                                         when= = the item is hashed / dumped only while every C (OPND OP OPND without
 *                                         spaces, e.g. .type==1) holds: a union member picked by a discriminator
 *   full PATH [..]                        a full-tier item (ACNB_STATE_FULL=0 skips it): the full walk of a root
 *   info PATH [..]                        dumped (area NAME/info) with the full tier, never hashed: untyped union
 *                                         words (no cross-build meaning: a data word can fall inside a symbol /
 *                                         the heap in one build only - garage6 mobj dt 0x00420000 = ReplayBuffer+..
 *                                         in the hybrid, raw in the source port)
 *   ov                                    item: the backend's overlay id (0 none, 1 OLSORTIE, 2 OLMENU)
 * PATH = SYMBOL (NAME, FILE.c:NAME, @0xADDR; platform/names) then steps: [N] index, .m member, ->S follow the
 * pointer as struct S, -> follow untyped (then +N and type=), +N byte offset (type then unknown -> type=). A bitfield
 * member may end a path (one leaf; usable as a cond operand: `live .f_use`).
 * COND = OPND [== != < <= > >= OPND]; OPND = integer, $i (substituted), ov, a gate name, &NAME (the address of a
 * function / data symbol in this build: `.play_func==&__PlayBlinkSprite`), or a scalar PATH (a pointer compares as its
 * raw value); a bare OPND means != 0. Padding members (_pad*, _reserved*, dummy*, pad, padN, reserved) are skipped in struct expansion;
 * a union (all members at offset 0) is hashed as raw bytes, or, when one of its members holds a pointer, as 32-bit
 * words that hash as their identity when it resolves (pool / symbol) and as the raw word otherwise.
 *
 * Pointer identity (section 3): 0 -> "0"; inside an active pool -> "POOL:IDX.member" (rule 2); inside a table symbol
 * (function or data of the main ELF / the loaded overlay) -> "NAME" / "NAME+0xOFF" (rules 3 / 4; a `heap` symbol:
 * "H" in core, the offset in full); else "H". Never the address. Cost: the time per vblank is logged every 1000 vblanks (`statehash: cost ..`). */
#include "statehash.h"
#include "../names/names.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <direct.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

enum { K_INT, K_UINT, K_F32, K_F64, K_RAW, K_BF, K_PTR, K_OV, K_WORD };
struct Leaf { uint32_t off; uint16_t n; uint8_t kind, boff, bits; char *name; };
enum { MAXD = 6 };
struct Opnd { int kind; long long v; int item; };   /* kind 0 literal, 1 ov, 2 item (scalar), 3 gate */
struct Item {
    uint32_t root; int nd; uint32_t doff[MAXD]; int l0, nl; char *path;
    uint8_t rel, full, aux, bounded, info;   /* rel: root = offset in the current pool element; aux: a cond operand */
    uint32_t ecnt; Opnd n, at; int has_at;
    int wc0, wnc;                      /* when= conds (0 = always) */
};
struct Cond { Opnd a, b; int op; };                 /* op 0 = bare (a != 0), 1 == 2 != 3 < 4 <= 5 > 6 >= */
struct Gate { char *name; int c0, nc; };
struct Pool {
    char *name, *etype; int item, is_ptr, unordered, has_n, c0, nc; uint32_t esz, scnt; Opnd n;
    uint32_t base, count; int on;   /* this vblank */
};
struct Area { char *name; int c0, nc, i0, ni, pool, from, lc0, lnc, ordered; uint64_t h, hf; uint32_t live; int on; };
struct MRule { char *s, *m; int act; };
struct Alias { char *tbl, *name; int k; uint32_t a; int done; };   /* a = the slot's value, read on first use */            /* act 0 keep, 1 skip, 2 ptr, 3 words, 100 + N first N */

template <class T> T *grow(T *&a, int &n, int &cap)
{
    if (n == cap) { cap = cap ? cap * 2 : 64; a = (T *)realloc(a, (size_t)cap * sizeof(T)); }
    memset(&a[n], 0, sizeof(T));
    return &a[n++];
}

Leaf *leaves; int nleaves, cap_leaves;
Item *items; int nitems, cap_items;
Cond *conds; int nconds, cap_conds;
Gate *gates; int ngates, cap_gates;
Area *areas; int nareas, cap_areas;
Pool *pools; int npools, cap_pools;
MRule *mrules; int nmrules, cap_mrules;
Alias *aliases; int naliases, cap_aliases;
char *heaps[8]; int nheaps;

const SbxBackend *be;
int state = -1;   /* -1 not loaded, 0 off (error), 1 on */
uint32_t frame_addr;
int has_frame;
char err[600];
int cur_ovl;
int full_on = 1;                 /* ACNB_STATE_FULL (default on) */
const char *cur_etype;           /* compile: element type of the each-area being compiled (relative items) */
uint32_t cur_elem;               /* run: address of the current pool element */

/* dump selection */
uint32_t dump_list[64]; int ndump;
uint32_t dump_a = 1, dump_b = 0;
const char *dump_dir = "state";

/* cost */
LARGE_INTEGER qpf;
double cost_sum, cost_max;
uint32_t cost_n, cost_vb0;

/* folded functions (source port identical-code folding: two rows, one address) already reported */
uint32_t fold_seen[64]; int nfold_seen;

void log_line(const char *fmt, const char *a = "", const char *b = "", int n = 0)
{
    char line[900];
    snprintf(line, sizeof line, fmt, a, b, n);
    be->log(line);
}

uint64_t fnv64(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 1099511628211ull;
    return h;
}
inline void feed(uint64_t &h, uint64_t v)
{
    h ^= v;
    h *= 0x9e3779b97f4a7c15ull;
    h ^= h >> 29;
}
inline uint64_t fmix(uint64_t h)
{
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
    return h;
}

int rd(uint32_t a, uint32_t n, void *out)
{
    const void *p = be->ptr(a, n);
    if (!p) return 0;
    memcpy(out, p, n);
    return 1;
}

/* ---- compile ---- */

int is_pad(const char *n)
{
    if (!strncmp(n, "_pad", 4) || !strncmp(n, "_reserved", 9) || !strncmp(n, "dummy", 5) || !strcmp(n, "reserved")) return 1;
    if (strncmp(n, "pad", 3)) return 0;
    for (const char *s = n + 3; *s; s++) if (!isdigit((unsigned char)*s) && *s != '_') return 0;
    return 1;   /* pad, pad0, pad_1 (not padoff) */
}

int mrule(const char *s, const char *m)
{
    for (int k = 0; k < nmrules; k++) if (!strcmp(mrules[k].s, s) && !strcmp(mrules[k].m, m)) return mrules[k].act;
    return -1;
}

int add_leaf(int kind, uint32_t off, uint32_t n, const char *name, int boff = 0, int bits = 0)
{
    Leaf *l = grow(leaves, nleaves, cap_leaves);
    l->kind = (uint8_t)kind; l->off = off; l->n = (uint16_t)n; l->boff = (uint8_t)boff; l->bits = (uint8_t)bits;
    l->name = strdup(name);
    return 1;
}

int scalar(const char *t, int *kind, uint32_t *n)
{
    static const struct { const char *t; int k; uint32_t n; } tab[] = {
        {"s8", K_INT, 1}, {"s16", K_INT, 2}, {"s32", K_INT, 4}, {"s64", K_INT, 8}, {"u8", K_UINT, 1}, {"u16", K_UINT, 2},
        {"u32", K_UINT, 4}, {"u64", K_UINT, 8}, {"f32", K_F32, 4}, {"f64", K_F64, 8}, {"u128", K_RAW, 16}, {"p", K_PTR, 4}};
    for (auto &e : tab) if (!strcmp(t, e.t)) { *kind = e.k; *n = e.n; return 1; }
    if (t[0] == 'x' && isdigit((unsigned char)t[1])) { *kind = K_RAW; *n = (uint32_t)atoi(t + 1); return 1; }
    return 0;
}

const char *elem_type(const char *t)   /* aN:aM:T -> T */
{
    while (t[0] == 'a' && isdigit((unsigned char)t[1])) {
        const char *c = strchr(t, ':');
        if (!c) break;
        t = c + 1;
    }
    return t;
}

/* does a value of type t hold a pointer anywhere (unions: words checked for identity) */
int has_ptr(const char *t, int depth)
{
    t = elem_type(t);
    if (!strcmp(t, "p")) return 1;
    if (depth > 16 || t[0] != 's' || t[1] != ':') return 0;
    uint32_t f, c, sz;
    int en;
    if (!sbx_agg(t + 2, &f, &c, &sz, &en) || en) return 0;
    for (uint32_t i = 0; i < c; i++) {
        const char *mn, *mt;
        uint32_t mo;
        sbx_member(f + i, &mn, &mo, &mt);
        if (strncmp(mt, "bf:", 3) && has_ptr(mt, depth + 1)) return 1;
    }
    return 0;
}

/* all leaves of type t at off; name = buffer holding the path so far (len used) */
int expand(const char *t, uint32_t off, char *name, size_t len, size_t cap, int depth)
{
    int kind;
    uint32_t n;
    if (depth > 24) { snprintf(err, sizeof err, "%s: type nesting too deep", name); return 0; }
    if (scalar(t, &kind, &n)) return add_leaf(kind, off, n, name);
    if (t[0] == 'a' && isdigit((unsigned char)t[1])) {
        char *e;
        uint32_t cnt = (uint32_t)strtoul(t + 1, &e, 10), es = sbx_type_size(e + 1);
        if (*e != ':' || !es) { snprintf(err, sizeof err, "%s: array of unknown size (%s)", name, t); return 0; }
        for (uint32_t i = 0; i < cnt; i++) {
            snprintf(name + len, cap - len, "[%u]", i);
            if (!expand(e + 1, off + i * es, name, strlen(name), cap, depth + 1)) return 0;
        }
        name[len] = 0;
        return 1;
    }
    if (t[0] == 'e' && t[1] == ':') {
        uint32_t f, c, sz;
        int en;
        if (!sbx_agg(t + 2, &f, &c, &sz, &en)) sz = 4;
        return add_leaf(K_INT, off, sz ? sz : 4, name);
    }
    if (t[0] == 's' && t[1] == ':') {
        uint32_t f, c, sz;
        int en;
        if (!sbx_agg(t + 2, &f, &c, &sz, &en)) { snprintf(err, sizeof err, "%s: unknown struct %s", name, t + 2); return 0; }
        if (en) return add_leaf(K_INT, off, sz ? sz : 4, name);
        int uni = c > 1;
        for (uint32_t i = 0; i < c && uni; i++) {
            const char *mn, *mt;
            uint32_t mo;
            sbx_member(f + i, &mn, &mo, &mt);
            if (mo) uni = 0;
        }
        if (uni) {
            snprintf(name + len, cap - len, "(union)");
            add_leaf(has_ptr(t, 0) ? K_WORD : K_RAW, off, sz, name);
            name[len] = 0;
            return 1;
        }
        for (uint32_t i = 0; i < c; i++) {
            const char *mn, *mt;
            uint32_t mo;
            sbx_member(f + i, &mn, &mo, &mt);
            int r = mrule(t + 2, mn);
            if (r == 1 || (r != 0 && is_pad(mn))) continue;
            snprintf(name + len, cap - len, ".%s", mn);
            if (r == 2) add_leaf(K_PTR, off + mo, 4, name);
            else if (r == 3) {
                uint32_t msz = 0;
                if (i + 1 < c) { const char *n2, *t2; uint32_t o2; sbx_member(f + i + 1, &n2, &o2, &t2); msz = o2 - mo; }
                else msz = sz - mo;
                add_leaf(K_WORD, off + mo, msz & ~3u, name);
            } else if (r >= 100 && mt[0] == 'a' && isdigit((unsigned char)mt[1])) {
                char *e;
                uint32_t cnt = (uint32_t)strtoul(mt + 1, &e, 10), nk = (uint32_t)(r - 100);
                char t2[200];
                snprintf(t2, sizeof t2, "a%u%s", nk < cnt ? nk : cnt, e);
                if (!expand(t2, off + mo, name, strlen(name), cap, depth + 1)) return 0;
            }
            else if (!strncmp(mt, "bf:", 3)) {   /* bf:BITS:BITOFF:T (sbx_types.tsv bit column; MW bitfields LSB-first) */
                char *e;
                int bits = (int)strtol(mt + 3, &e, 10), boff = (int)strtol(e + 1, &e, 10);
                uint32_t cn = 0;
                int ck;
                if (!scalar(e + 1, &ck, &cn) || cn > 8) { snprintf(err, sizeof err, "%s: bad bitfield %s", name, mt); return 0; }
                add_leaf(K_BF, off + mo, cn, name, boff, bits);
            } else if (!expand(mt, off + mo, name, strlen(name), cap, depth + 1)) return 0;
        }
        name[len] = 0;
        return 1;
    }
    snprintf(err, sizeof err, "%s: no hashable type (%s; give type=T)", name, t);
    return 0;
}

int idc(int c) { return c == '_' || isalnum((unsigned char)c); }

/* PATH [type=T] -> item (+ its leaves when want); 1 = ok; *tout = the path's type (before type=) */
int compile_item(const char *path, const char *type_over, int *out, int want = 1, const char **tout = 0)
{
    Item it;
    memset(&it, 0, sizeof it);
    const char *s = path, *t = 0, *e2;
    char id[300];
    uint32_t *acc = &it.root;
    int bf_bits = 0, bf_off = 0;
    uint32_t bf_n = 0;
    if (!strcmp(path, "ov")) {
        it.l0 = nleaves;
        add_leaf(K_OV, 0, 1, "ov");
        it.nl = 1;
        it.path = strdup(path);
        *grow(items, nitems, cap_items) = it;
        *out = nitems - 1;
        return 1;
    }
    if (cur_etype && (s[0] == '.' || s[0] == '[' || (s[0] == '-' && s[1] == '>'))) {   /* relative to the element */
        it.rel = 1;
        t = cur_etype;
        if (!strcmp(s, ".")) s++;
    } else {   /* symbol: [FILE.c:]NAME or @0xADDR */
        const char *p = s, *qual = 0, *colon = strchr(s, ':');
        size_t qn = 0;
        if (colon && strcspn(s, "[-+") > (size_t)(colon - s)) { qual = s; qn = (size_t)(colon - s); p = colon + 1; }
        const char *nm = p;
        if (*p == '@') p++;
        while (idc(*p)) p++;
        size_t n = (size_t)(p - nm);
        if (!n || n >= sizeof id) { snprintf(err, sizeof err, "bad symbol in %s", path); return 0; }
        memcpy(id, nm, n);
        id[n] = 0;
        int ok = id[0] == '@' ? sbx_lookup_ps2((uint32_t)strtoul(id + 1, 0, 0), &it.root, &t, &e2)
                              : sbx_lookup(qual, qn, id, &it.root, &t, &e2);
        if (!ok) { snprintf(err, sizeof err, "%s: %s", path, e2); return 0; }
        if (!t) t = "-";
        s = p;
    }
    while (*s) {
        if (*s == '[') {
            char *e;
            long i = strtol(s + 1, &e, 0);
            if (*e != ']' || e == s + 1) { snprintf(err, sizeof err, "%s: bad index", path); return 0; }
            if (t[0] != 'a' || !isdigit((unsigned char)t[1])) { snprintf(err, sizeof err, "%s: [..] on a non-array (%s)", path, t); return 0; }
            char *te;
            unsigned long cnt = strtoul(t + 1, &te, 10);
            /* a0 = DWARF array without a bound (STAGE_ModelNum): any index */
            if (i < 0 || (cnt && (unsigned long)i >= cnt)) { snprintf(err, sizeof err, "%s: index %ld out of range (%s)", path, i, t); return 0; }
            t = te + 1;
            *acc += (uint32_t)i * sbx_type_size(t);
            s = e + 1;
        } else if (*s == '.') {
            const char *m = ++s;
            while (idc(*s)) s++;
            uint32_t f, c, sz, mo;
            int en, found = 0;
            const char *mn, *mt;
            if (t[0] == 's' && t[1] == ':' && sbx_agg(t + 2, &f, &c, &sz, &en))
                for (uint32_t k = 0; k < c && !found; k++) {
                    sbx_member(f + k, &mn, &mo, &mt);
                    if (strlen(mn) == (size_t)(s - m) && !memcmp(mn, m, (size_t)(s - m))) found = 1;
                }
            if (!found) { snprintf(err, sizeof err, "%s: no member %.*s in %s", path, (int)(s - m), m, t); return 0; }
            if (!strncmp(mt, "bf:", 3)) {   /* bf:BITS:BITOFF:T - only as the last step (one K_BF leaf) */
                char *e;
                int ck;
                bf_bits = (int)strtol(mt + 3, &e, 10);
                bf_off = (int)strtol(e + 1, &e, 10);
                if (*s || !scalar(e + 1, &ck, &bf_n) || bf_n > 8 || bf_bits <= 0) {
                    snprintf(err, sizeof err, "%s: a bitfield must end the path (%s)", path, mt);
                    return 0;
                }
                *acc += mo;
                t = "bf";
                continue;
            }
            *acc += mo;
            t = mt;
        } else if (s[0] == '-' && s[1] == '>') {
            if (strcmp(t, "p") && strcmp(t, "u32") && strcmp(t, "s32") && strcmp(t, "-")) {
                snprintf(err, sizeof err, "%s: -> on a non-pointer (%s)", path, t);
                return 0;
            }
            if (it.nd == MAXD) { snprintf(err, sizeof err, "%s: too many ->", path); return 0; }
            acc = &it.doff[it.nd++];
            s += 2;
            const char *m = s;
            while (idc(*s)) s++;
            if (s > m) {
                char b[128];
                snprintf(b, sizeof b, "s:%.*s", (int)(s - m), m);
                uint32_t f, c, sz;
                int en;
                if (!sbx_agg(b + 2, &f, &c, &sz, &en)) { snprintf(err, sizeof err, "%s: unknown struct %s", path, b + 2); return 0; }
                t = strdup(b);
            } else t = "-";
        } else if (*s == '+') {
            char *e;
            unsigned long o = strtoul(s + 1, &e, 0);
            if (e == s + 1) { snprintf(err, sizeof err, "%s: bad +offset", path); return 0; }
            *acc += (uint32_t)o;
            t = "-";
            s = e;
        } else { snprintf(err, sizeof err, "%s: junk at \"%s\"", path, s); return 0; }
    }
    if (tout) *tout = t;
    it.l0 = nleaves;
    if (bf_bits) {
        if (type_over) { snprintf(err, sizeof err, "%s: type= on a bitfield", path); return 0; }
        if (want) add_leaf(K_BF, 0, bf_n, path, bf_off, bf_bits);
    } else if (type_over) t = type_over;
    if (want && !bf_bits) {
        char name[700];
        snprintf(name, sizeof name, "%s", path);
        if (!expand(t, 0, name, strlen(name), sizeof name, 0)) return 0;
    }
    it.nl = nleaves - it.l0;
    it.path = strdup(path);
    *grow(items, nitems, cap_items) = it;
    *out = nitems - 1;
    return 1;
}

int compile_opnd(const char *s, Opnd *o)
{
    char *e;
    long long v = strtoll(s, &e, 0);
    if (e != s && !*e) { o->kind = 0; o->v = v; return 1; }
    if (!strcmp(s, "ov")) { o->kind = 1; return 1; }
    if (s[0] == '&') {   /* &NAME: the symbol's address in this build (compared with a pointer field's raw value) */
        uint32_t a;
        const char *e2;
        if (!sbx_resolve(s + 1, &a, &e2)) { snprintf(err, sizeof err, "%s: %s", s, e2); return 0; }
        o->kind = 0; o->v = (long long)a;
        return 1;
    }
    for (int g = 0; g < ngates; g++) if (!strcmp(gates[g].name, s)) { o->kind = 3; o->v = g; return 1; }
    int it;
    if (!compile_item(s, 0, &it)) return 0;
    items[it].aux = 1;   /* an operand, not hashed (may sit between an area's items) */
    if (items[it].nl != 1 || leaves[items[it].l0].kind == K_RAW || leaves[items[it].l0].kind == K_F64
        || leaves[items[it].l0].kind == K_WORD) {
        snprintf(err, sizeof err, "%s: a condition needs one integer field", s);
        return 0;
    }
    o->kind = 2; o->item = it;
    return 1;
}

/* "A op B && C .." -> conds; *c0 / *nc. Operand items are compiled first, then the conds (consecutive). */
int compile_conds(char *s, int *c0, int *nc)
{
    Cond cs[16];
    int n = 0;
    while (s && *s) {
        char *amp = strstr(s, "&&");
        if (amp) *amp = 0;
        char *tok[4];
        int nt = 0;
        for (char *p = strtok(s, " \t"); p; p = strtok(0, " \t")) { if (nt == 4) break; tok[nt++] = p; }
        static const char *ops[] = {"", "==", "!=", "<", "<=", ">", ">="};
        Cond c;
        memset(&c, 0, sizeof c);
        if (nt == 1) c.op = 0;
        else if (nt == 3) {
            for (int k = 1; k < 7; k++) if (!strcmp(tok[1], ops[k])) c.op = k;
            if (!c.op) { snprintf(err, sizeof err, "bad operator %s", tok[1]); return 0; }
        } else { snprintf(err, sizeof err, "bad condition (A op B with spaces)"); return 0; }
        if (n == 16) { snprintf(err, sizeof err, "too many && terms"); return 0; }
        char t0[300], t2[300];
        snprintf(t0, sizeof t0, "%s", tok[0]);
        if (nt == 3) snprintf(t2, sizeof t2, "%s", tok[2]);
        if (!compile_opnd(t0, &c.a)) return 0;
        if (nt == 3 && !compile_opnd(t2, &c.b)) return 0;
        cs[n++] = c;
        s = amp ? amp + 2 : 0;
    }
    *c0 = nconds; *nc = n;
    for (int k = 0; k < n; k++) *grow(conds, nconds, cap_conds) = cs[k];
    return 1;
}

/* text with $i replaced by i */
void subst(const char *in, int i, char *out, size_t cap)
{
    size_t o = 0;
    for (const char *s = in; *s && o + 12 < cap; s++) {
        if (s[0] == '$' && s[1] == 'i') { o += (size_t)snprintf(out + o, cap - o, "%d", i); s++; }
        else out[o++] = *s;
    }
    out[o] = 0;
}

void trim(char *s)
{
    char *h = strchr(s, '#');
    if (h) *h = 0;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    size_t k = 0;
    while (isspace((unsigned char)s[k])) k++;
    memmove(s, s + k, strlen(s + k) + 1);
}

/* one item line ([full] PATH [type=T] [n=OPND] [at=OPND]); [A..B] in the path = one item per index */
int compile_line(const char *line, int lineno)
{
    int full = 0, info = 0;
    if (!strncmp(line, "full ", 5) || !strncmp(line, "info ", 5)) {
        full = 1;
        info = line[0] == 'i';
        line += 5;
        while (*line == ' ' || *line == '\t') line++;
    }
    char buf[1200];
    snprintf(buf, sizeof buf, "%s", line);
    char *path = strtok(buf, " \t"), *opt[4];
    int no = 0;
    for (char *p = strtok(0, " \t"); p && no < 4; p = strtok(0, " \t")) opt[no++] = p;
    const char *r = path ? strstr(path, "..") : 0;
    if (r) {
        const char *lb = r;
        while (lb > path && *lb != '[') lb--;
        char *e;
        long a = strtol(lb + 1, &e, 10), b = e == r ? strtol(r + 2, &e, 10) : -1;
        if (*lb != '[' || *e != ']' || b < a) { snprintf(err, sizeof err, "line %d: bad range in %s", lineno, line); return 0; }
        const char *rest = line + strlen(path);
        for (long v = a; v <= b; v++) {
            char nb[1200];
            snprintf(nb, sizeof nb, "%s%.*s[%ld]%s%s", info ? "info " : full ? "full " : "", (int)(lb - path), path, v, e + 1, rest);
            if (!compile_line(nb, lineno)) return 0;
        }
        return 1;
    }
    if (!path) { snprintf(err, sizeof err, "line %d: empty item", lineno); return 0; }
    const char *tov = 0, *nexp = 0, *atexp = 0, *wexp = 0;
    for (int k = 0; k < no; k++) {
        if (!strncmp(opt[k], "type=", 5)) tov = opt[k] + 5;
        else if (!strncmp(opt[k], "when=", 5)) wexp = opt[k] + 5;
        else if (!strncmp(opt[k], "n=", 2)) nexp = opt[k] + 2;
        else if (!strncmp(opt[k], "at=", 3)) atexp = opt[k] + 3;
        else { snprintf(err, sizeof err, "line %d: unknown option %s", lineno, opt[k]); return 0; }
    }
    int it;
    const char *t;
    if (!compile_item(path, tov, &it, 1, &t)) return 0;
    items[it].full = (uint8_t)full;
    items[it].info = (uint8_t)info;
    if (wexp) {   /* "A==1,B!=2" -> "A == 1 && B != 2" */
        char cb[1000];
        size_t o = 0;
        for (const char *p = wexp; *p && o + 8 < sizeof cb;) {
            const char *c = strchr(p, ','), *end = c ? c : p + strlen(p);
            static const char *ops[] = {"==", "!=", "<=", ">=", "<", ">"};
            const char *op = 0, *at = 0;
            for (const char *q = p; q < end && !op; q++)
                for (const char *x : ops) if (!strncmp(q, x, strlen(x))) { op = x; at = q; break; }
            if (o) o += (size_t)snprintf(cb + o, sizeof cb - o, " && ");
            if (op) o += (size_t)snprintf(cb + o, sizeof cb - o, "%.*s %s %.*s", (int)(at - p), p, op,
                                          (int)(end - at - strlen(op)), at + strlen(op));
            else o += (size_t)snprintf(cb + o, sizeof cb - o, "%.*s", (int)(end - p), p);
            p = c ? c + 1 : end;
        }
        int c0, nc;
        if (!compile_conds(cb, &c0, &nc)) return 0;
        items[it].wc0 = c0; items[it].wnc = nc;
    }
    if (nexp || atexp) {
        if (tov) t = tov;   /* type=aN:T reinterprets a buffer as an array (stCom.mng_buff) */
        if (t[0] != 'a' || !isdigit((unsigned char)t[1])) { snprintf(err, sizeof err, "line %d: n= / at= need an array (%s)", lineno, t); return 0; }
        Opnd n, at;
        memset(&at, 0, sizeof at);
        n.kind = 0; n.v = (long long)strtoul(t + 1, 0, 10);
        if (nexp && !compile_opnd(nexp, &n)) return 0;
        if (atexp && !compile_opnd(atexp, &at)) return 0;
        Item &x = items[it];
        x.bounded = 1;
        x.ecnt = (uint32_t)strtoul(t + 1, 0, 10);
        x.n = n; x.at = at; x.has_at = atexp != 0;
        if (!x.ecnt || x.nl % x.ecnt) { snprintf(err, sizeof err, "line %d: bad bounded array %s", lineno, path); return 0; }
    }
    return 1;
}

int find_pool(const char *n)
{
    for (int k = 0; k < npools; k++) if (!strcmp(pools[k].name, n)) return k;
    return -1;
}

/* pool NAME PATH [type=S] [n=OPND] [unordered] [if COND ..] */
int compile_pool(char *p, int ln)
{
    Pool pl;
    memset(&pl, 0, sizeof pl);
    char *ifp = strstr(p, " if ");
    char cond[1000] = "";
    if (ifp) { snprintf(cond, sizeof cond, "%s", ifp + 4); *ifp = 0; }
    char *tok[8];
    int nt = 0;
    for (char *q = strtok(p, " \t"); q && nt < 8; q = strtok(0, " \t")) tok[nt++] = q;
    if (nt < 2) { snprintf(err, sizeof err, "line %d: pool NAME PATH ..", ln); return 0; }
    char nm[200], path[400], ty[200] = "", ns[300] = "";
    snprintf(nm, sizeof nm, "%s", tok[0]);
    snprintf(path, sizeof path, "%s", tok[1]);
    for (int k = 2; k < nt; k++) {
        if (!strncmp(tok[k], "type=", 5)) snprintf(ty, sizeof ty, "%s", tok[k] + 5);
        else if (!strncmp(tok[k], "n=", 2)) snprintf(ns, sizeof ns, "%s", tok[k] + 2);
        else if (!strcmp(tok[k], "unordered")) pl.unordered = 1;
        else { snprintf(err, sizeof err, "line %d: unknown pool option %s", ln, tok[k]); return 0; }
    }
    if (find_pool(nm) >= 0) { snprintf(err, sizeof err, "line %d: pool %s twice", ln, nm); return 0; }
    const char *t;
    if (!compile_item(path, 0, &pl.item, 0, &t)) return 0;
    items[pl.item].aux = 1;
    if (t[0] == 'a' && isdigit((unsigned char)t[1])) {   /* static array */
        char *e;
        pl.scnt = (uint32_t)strtoul(t + 1, &e, 10);
        pl.etype = strdup(e + 1);
    } else if (!strcmp(t, "p") || !strcmp(t, "u32") || !strcmp(t, "-")) {
        if (!ty[0] || !ns[0]) { snprintf(err, sizeof err, "line %d: pointer pool %s needs type= and n=", ln, nm); return 0; }
        pl.is_ptr = 1;
    } else { snprintf(err, sizeof err, "line %d: pool %s: %s is neither an array nor a pointer", ln, nm, t); return 0; }
    if (ty[0]) {
        char b[220];
        snprintf(b, sizeof b, "s:%s", ty);
        free(pl.etype);
        pl.etype = strdup(b);
    }
    pl.esz = sbx_type_size(pl.etype);
    if (!pl.esz) { snprintf(err, sizeof err, "line %d: pool %s: element size unknown (%s)", ln, nm, pl.etype); return 0; }
    if (ns[0]) { if (!compile_opnd(ns, &pl.n)) return 0; pl.has_n = 1; }
    if (!compile_conds(cond, &pl.c0, &pl.nc)) return 0;
    pl.name = strdup(nm);
    *grow(pools, npools, cap_pools) = pl;
    return 1;
}

/* one area block (header + item lines) for copy i */
int compile_area(const char *hdr_name, const char *hdr_cond, int pool, int from, int ordered, char **lines, int nl, int i,
                 int lineno)
{
    char buf[1200];
    Area a;
    memset(&a, 0, sizeof a);
    subst(hdr_name, i, buf, sizeof buf);
    a.name = strdup(buf);
    a.pool = pool;
    a.from = from;
    a.ordered = ordered;
    subst(hdr_cond, i, buf, sizeof buf);
    if (!compile_conds(buf, &a.c0, &a.nc)) return 0;
    cur_etype = pool >= 0 ? pools[pool].etype : 0;
    a.i0 = nitems;   /* item lines compile to consecutive items; cond operands in between are aux (skipped) */
    for (int k = 0; k < nl; k++) {
        subst(lines[k], i, buf, sizeof buf);
        if (!strncmp(buf, "live ", 5)) {
            if (pool < 0) { snprintf(err, sizeof err, "line %d: live outside an each-area", lineno); cur_etype = 0; return 0; }
            if (!compile_conds(buf + 5, &a.lc0, &a.lnc)) { cur_etype = 0; return 0; }
            continue;
        }
        if (!compile_line(buf, lineno)) { cur_etype = 0; return 0; }
    }
    cur_etype = 0;
    a.ni = nitems - a.i0;
    *grow(areas, nareas, cap_areas) = a;
    return 1;
}

int load_fields(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, sizeof err, "no field file %s", path); return 0; }
    char line[1200];
    static char *blk[8192];
    int nb = 0, copies = 0, ln = 0, hdr_ln = 0, ok = 1, hpool = -1, hfrom = 0, hord = 0;
    char hname[200] = "", hcond[1000] = "";
    auto flush = [&]() -> int {
        if (!copies) return 1;
        for (int i = 0; i < copies; i++)
            if (!compile_area(hname, hcond, hpool, hfrom, hord, blk, nb, i, hdr_ln)) return 0;
        for (int k = 0; k < nb; k++) free(blk[k]);
        nb = 0; copies = 0;
        return 1;
    };
    while (ok && fgets(line, sizeof line, f)) {
        ln++;
        trim(line);
        if (!line[0]) continue;
        if (!strncmp(line, "area ", 5) || !strncmp(line, "gate ", 5) || !strncmp(line, "pool ", 5) || !strncmp(line, "member ", 7)
            || !strncmp(line, "alias ", 6) || !strncmp(line, "heap ", 5)) {
            if (!(ok = flush())) break;
            if (line[0] == 'h') {
                char hn[200];
                uint32_t ha;
                const char *ht, *he;
                if (sscanf(line + 5, "%199s", hn) != 1 || nheaps == 8) { snprintf(err, sizeof err, "line %d: heap SYMBOL", ln); ok = 0; break; }
                if (!sbx_lookup(0, 0, hn, &ha, &ht, &he)) { snprintf(err, sizeof err, "line %d: heap %s: %s", ln, hn, he); ok = 0; break; }
                heaps[nheaps++] = strdup(hn);
                continue;
            }
            if (line[0] == 'a' && line[1] == 'l') {
                char tb[200], nm2[200];
                int k = -1;
                if (sscanf(line + 6, "%199s %d %199s", tb, &k, nm2) != 3 || k < 0) { snprintf(err, sizeof err, "line %d: alias TABLE K NAME", ln); ok = 0; break; }
                Alias *al = grow(aliases, naliases, cap_aliases);
                al->tbl = strdup(tb); al->name = strdup(nm2); al->k = k; al->a = 0; al->done = 0;
                continue;
            }
            if (line[0] == 'p') { if (!(ok = compile_pool(line + 5, ln))) break; continue; }
            if (line[0] == 'm') {
                char s[200], act[40];
                if (sscanf(line + 7, "%199s %39s", s, act) != 2 || !strchr(s, '.')) { snprintf(err, sizeof err, "line %d: member STRUCT.MEMBER keep|skip|ptr|words|nN", ln); ok = 0; break; }
                int ac = !strcmp(act, "keep") ? 0 : !strcmp(act, "skip") ? 1 : !strcmp(act, "ptr") ? 2 : !strcmp(act, "words") ? 3
                       : (act[0] == 'n' && atoi(act + 1) > 0) ? 100 + atoi(act + 1) : -1;
                if (ac < 0) { snprintf(err, sizeof err, "line %d: member action %s", ln, act); ok = 0; break; }
                MRule *r = grow(mrules, nmrules, cap_mrules);
                *strchr(s, '.') = 0;
                r->s = strdup(s); r->m = strdup(s + strlen(s) + 1); r->act = ac;
                continue;
            }
            char *p = line + 5;
            while (*p == ' ') p++;
            char *nm = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = 0;
            while (*p == ' ' || *p == '\t') p++;
            if (line[0] == 'g') {
                char c[1000];
                snprintf(c, sizeof c, "%s", p);
                Gate g;
                g.name = strdup(nm);
                if (!(ok = compile_conds(c, &g.c0, &g.nc))) break;
                *grow(gates, ngates, cap_gates) = g;
                continue;
            }
            snprintf(hname, sizeof hname, "%s", nm);
            copies = 1; hpool = -1; hfrom = 0; hord = 0; hcond[0] = 0;
            while (*p) {   /* [xN] [each POOL [from K]] [if COND] */
                if (p[0] == 'x' && isdigit((unsigned char)p[1])) {
                    copies = atoi(p + 1);
                    while (*p && *p != ' ') p++;
                } else if (!strncmp(p, "each ", 5)) {
                    char pn[200];
                    p += 5;
                    int k = 0;
                    while (*p && *p != ' ' && k < 199) pn[k++] = *p++;
                    pn[k] = 0;
                    if ((hpool = find_pool(pn)) < 0) { snprintf(err, sizeof err, "line %d: unknown pool %s", ln, pn); ok = 0; break; }
                } else if (!strncmp(p, "from ", 5)) {
                    hfrom = (int)strtol(p + 5, &p, 0);
                } else if (!strncmp(p, "ordered", 7) && (!p[7] || p[7] == ' ')) {
                    hord = 1;
                    p += 7;
                } else if (!strncmp(p, "if ", 3)) {
                    snprintf(hcond, sizeof hcond, "%s", p + 3);
                    break;
                } else { snprintf(err, sizeof err, "line %d: expected xN / each POOL / from K / ordered / if COND", ln); ok = 0; break; }
                while (*p == ' ') p++;
            }
            if (!ok) break;
            if (copies > 1 && hpool >= 0) { snprintf(err, sizeof err, "line %d: xN and each together", ln); ok = 0; break; }
            if (hord && hpool < 0) { snprintf(err, sizeof err, "line %d: ordered without each", ln); ok = 0; break; }
            hdr_ln = ln;
            continue;
        }
        if (!copies) { snprintf(err, sizeof err, "line %d: item before the first area", ln); ok = 0; break; }
        if (nb < 8192) blk[nb++] = strdup(line);
    }
    if (ok) ok = flush();
    fclose(f);
    if (!ok) {
        char e2[600];
        snprintf(e2, sizeof e2, "%s: %s", path, err);
        memcpy(err, e2, sizeof err);
    }
    return ok;
}

/* ---- run ---- */

/* item base address: 1 ok, 0 null pointer on the chain, -1 unreadable */
int item_base(const Item &it, uint32_t *base)
{
    uint32_t a = it.rel ? cur_elem + it.root : it.root;
    for (int k = 0; k < it.nd; k++) {
        uint32_t v;
        if (!rd(a, 4, &v)) return -1;
        if (!v) return 0;
        a = v + it.doff[k];
    }
    *base = a;
    return 1;
}

long long opnd_val(const Opnd &o, int *ok);
int conds_true(int c0, int nc);

/* ".member[i].." of offset off inside type t (stops when the rest is 0: a pointer to a member's start names the
 * outermost member there); a remainder that no member covers -> "+0xN" */
void member_path(const char *t, uint32_t off, char *o, size_t cap)
{
    size_t n = strlen(o);
    for (int depth = 0; off && depth < 24; depth++) {
        if (t[0] == 'a' && isdigit((unsigned char)t[1])) {
            char *e;
            uint32_t cnt = (uint32_t)strtoul(t + 1, &e, 10), es = sbx_type_size(e + 1);
            if (!es || (cnt && off / es >= cnt)) break;
            n += (size_t)snprintf(o + n, n < cap ? cap - n : 0, "[%u]", off / es);
            off %= es;
            t = e + 1;
            continue;
        }
        if (t[0] != 's' || t[1] != ':') break;
        uint32_t f, c, sz;
        int en;
        if (!sbx_agg(t + 2, &f, &c, &sz, &en) || en) break;
        const char *bn = 0, *bt = 0;
        uint32_t bo = 0;
        for (uint32_t i = 0; i < c; i++) {
            const char *mn, *mt;
            uint32_t mo;
            sbx_member(f + i, &mn, &mo, &mt);
            if (!strncmp(mt, "bf:", 3) || mo > off) continue;
            uint32_t ms = sbx_type_size(mt);
            if (off < mo + (ms ? ms : 1) && (!bn || mo > bo)) { bn = mn; bt = mt; bo = mo; }
        }
        if (!bn) break;
        n += (size_t)snprintf(o + n, n < cap ? cap - n : 0, ".%s", bn);
        off -= bo;
        t = bt;
    }
    if (off && n < cap) snprintf(o + n, cap - n, "+0x%x", off);
}

void pools_update()
{
    for (int k = 0; k < npools; k++) {
        Pool &p = pools[k];
        p.on = 0;
        if (!conds_true(p.c0, p.nc)) continue;
        uint32_t a;
        if (item_base(items[p.item], &a) != 1) continue;
        if (p.is_ptr) {
            uint32_t v;
            if (!rd(a, 4, &v) || !v) continue;
            a = v;
        }
        long long n = p.scnt;
        if (p.has_n) {
            int ok = 1;
            n = opnd_val(p.n, &ok);
            if (!ok) continue;
        }
        if (n <= 0 || n > 0x100000) continue;
        p.base = a; p.count = (uint32_t)n; p.on = 1;
    }
}

/* identity string of pointer value p; core = the core tier (unordered pools without the index). Returns 1 when the
 * core form differs from the full one (p inside an unordered pool). */
int identity(uint32_t p, char *out, size_t cap, int core)
{
    if (!p) { snprintf(out, cap, "0"); return 0; }
    for (int k = 0; k < npools; k++) {
        const Pool &pl = pools[k];
        if (!pl.on || p < pl.base || p - pl.base >= pl.count * pl.esz) continue;
        uint32_t d = p - pl.base, idx = d / pl.esz;
        if (core && pl.unordered) snprintf(out, cap, "%s:*", pl.name);
        else snprintf(out, cap, "%s:%u", pl.name, idx);
        member_path(pl.etype, d % pl.esz, out, cap);
        return pl.unordered;
    }
    for (int k = 0; k < naliases; k++) {
        Alias &al = aliases[k];
        if (!al.done) {
            uint32_t t;
            const char *e;
            al.done = 1;
            if (!sbx_resolve(al.tbl, &t, &e) || !rd(t + 4u * (uint32_t)al.k, 4, &al.a)) al.a = 0;
        }
        if (al.a && al.a == p) { snprintf(out, cap, "%s", al.name); return 0; }
    }
    uint32_t off;
    const char *n = sbx_sym_at(p, cur_ovl, &off);
    for (int k = 0; n && core && k < nheaps; k++)
        if (!strcmp(n, heaps[k])) { snprintf(out, cap, "H"); return 1; }   /* core: heap pointers = "H" (rule 5) */
    if (!n) snprintf(out, cap, "H");
    else if (off) snprintf(out, cap, "%s+0x%x", n, off);
    else snprintf(out, cap, "%s", n);
    if (n && sbx_sym_fold) {   /* source port identical-code folding: the name is ambiguous (first row wins) */
        int seen = 0;
        for (int k = 0; k < nfold_seen; k++) seen |= fold_seen[k] == p - off;
        if (!seen && nfold_seen < 64) {
            fold_seen[nfold_seen++] = p - off;
            char m[400];
            snprintf(m, sizeof m, "statehash: WARNING %s shares its address with %s (folded code: identity ambiguous)", n, sbx_sym_fold);
            be->log(m);
        }
    }
    return 0;
}

/* one leaf: feeds h (core identity) and hf (full identity) when non-null; txt (dump, full identities) when given */
void leaf_feed(const Leaf &l, uint32_t base, uint64_t *h, uint64_t *hf, char *txt, size_t cap)
{
    uint8_t b[64];
#define FEED(v) do { uint64_t v_ = (v); if (h) feed(*h, v_); if (hf) feed(*hf, v_); } while (0)
    if (l.kind == K_OV) {
        FEED((uint64_t)cur_ovl);
        if (txt) snprintf(txt, cap, "%d", cur_ovl);
        return;
    }
    if (l.kind == K_WORD) {   /* union holding a pointer: 32-bit words, identity when it resolves, raw otherwise */
        uint32_t n = l.n;
        size_t o = 0;
        if (txt) txt[0] = 0;
        for (uint32_t k = 0; k + 4 <= n; k += 4) {
            uint32_t w;
            if (!rd(base + l.off + k, 4, &w)) { FEED(0x756e7265616462ull); continue; }
            char id[300], idf[300];
            int unord = w ? identity(w, id, sizeof id, 1) : 0;   /* core form differs: unordered pool / heap */
            if (w) { if (unord) identity(w, idf, sizeof idf, 0); else memcpy(idf, id, sizeof idf); }
            if (w && strcmp(idf, "H")) {   /* resolves (full form): identity; core gets the core form */
                if (h) feed(*h, fnv64(id));
                if (hf) feed(*hf, fnv64(idf));
                if (txt && o + 8 < cap) o += (size_t)snprintf(txt + o, cap - o, "%s%s", o ? " " : "", idf);
            } else {
                FEED(w);
                if (txt && o + 12 < cap) o += (size_t)snprintf(txt + o, cap - o, "%s%08x", o ? " " : "", w);
            }
        }
        return;
    }
    uint32_t n = l.n > sizeof b ? (uint32_t)sizeof b : l.n;
    if (!rd(base + l.off, n, b)) {
        FEED(0x756e7265616462ull);   /* "unreadb" */
        if (txt) snprintf(txt, cap, "unreadable");
        return;
    }
    uint64_t u = 0;
    if (n <= 8) memcpy(&u, b, n);
    switch (l.kind) {
    case K_INT: {
        long long v = n == 1 ? (int8_t)u : n == 2 ? (int16_t)u : n == 4 ? (int32_t)u : (long long)u;
        FEED((uint64_t)v);
        if (txt) snprintf(txt, cap, "%lld", v);
        break;
    }
    case K_UINT:
        FEED(u);
        if (txt) snprintf(txt, cap, "%llu", (unsigned long long)u);
        break;
    case K_F32: {
        float fv;
        uint32_t w = (uint32_t)u;
        memcpy(&fv, &w, 4);
        FEED(w);
        if (txt) snprintf(txt, cap, "0x%08x (%g)", w, (double)fv);
        break;
    }
    case K_F64:
        FEED(u);
        if (txt) snprintf(txt, cap, "0x%016llx", (unsigned long long)u);
        break;
    case K_BF: {
        uint64_t v = (u >> l.boff) & (l.bits >= 64 ? ~0ull : ((1ull << l.bits) - 1));
        FEED(v);
        if (txt) snprintf(txt, cap, "%llu", (unsigned long long)v);
        break;
    }
    case K_PTR: {
        char id[300], idf[300];
        int unord = identity((uint32_t)u, id, sizeof id, 1);
        if (h) feed(*h, fnv64(id));
        if (unord && (hf || txt)) identity((uint32_t)u, idf, sizeof idf, 0);
        if (hf) feed(*hf, fnv64(unord ? idf : id));
        if (txt) snprintf(txt, cap, "%s", unord ? idf : id);
        break;
    }
    default: {   /* raw bytes, 8 per word */
        size_t o = 0;
        if (txt) txt[0] = 0;
        for (uint32_t k = 0; k < n; k += 8) {
            uint64_t w = 0;
            memcpy(&w, b + k, n - k < 8 ? n - k : 8);
            FEED(w);
        }
        for (uint32_t k = 0; txt && k < n && o + 3 < cap; k++) o += (size_t)snprintf(txt + o, cap - o, "%02x", b[k]);
        break;
    }
    }
#undef FEED
}

long long opnd_val(const Opnd &o, int *ok)
{
    switch (o.kind) {
    case 0: return o.v;
    case 1: return cur_ovl;
    case 3: return conds_true(gates[o.v].c0, gates[o.v].nc);
    default: {
        const Item &it = items[o.item];
        const Leaf &l = leaves[it.l0];
        uint32_t base;
        uint8_t b[8] = {0};
        if (item_base(it, &base) != 1 || !rd(base + l.off, l.n, b)) { *ok = 0; return 0; }
        uint64_t u;
        memcpy(&u, b, 8);
        if (l.kind == K_INT) return l.n == 1 ? (int8_t)u : l.n == 2 ? (int16_t)u : l.n == 4 ? (int32_t)u : (long long)u;
        if (l.kind == K_BF) return (long long)((u >> l.boff) & ((1ull << l.bits) - 1));
        return l.n == 1 ? (long long)(uint8_t)u : l.n == 2 ? (long long)(uint16_t)u : l.n == 4 ? (long long)(uint32_t)u : (long long)u;
    }
    }
}

int conds_true(int c0, int nc)
{
    for (int k = c0; k < c0 + nc; k++) {
        const Cond &c = conds[k];
        int ok = 1;
        long long a = opnd_val(c.a, &ok), b = c.op ? opnd_val(c.b, &ok) : 0;
        if (!ok) return 0;
        int r = c.op == 0 ? a != 0 : c.op == 1 ? a == b : c.op == 2 ? a != b : c.op == 3 ? a < b : c.op == 4 ? a <= b
              : c.op == 5 ? a > b : a >= b;
        if (!r) return 0;
    }
    return 1;
}

/* full-tier static items without pointer leaves (a whole g_gamedata[i]: ~30k leaves) are hashed as one sub-hash
 * that is re-computed only when the item's bytes changed since the last vblank (byte compare against a copy; the
 * leaf values depend on those bytes only). Menu cost ST3: hybrid garage6 full ~0.9 ms per vblank -> . */
struct Memo { int st; uint32_t lo, n; uint8_t *buf; uint64_t h; int valid; };   /* st 0 unknown, 1 memo, 2 no */
Memo *memos; int nmemos;

Memo *memo_for(int k, const Item &it)
{
    if (k >= nmemos) {
        Memo *m = (Memo *)realloc(memos, sizeof(Memo) * (size_t)nitems);
        if (!m) return 0;
        memset(m + nmemos, 0, sizeof(Memo) * (size_t)(nitems - nmemos));
        memos = m;
        nmemos = nitems;
    }
    Memo &mo = memos[k];
    if (mo.st == 0) {
        mo.st = 2;
        if (it.nl < 64) return 0;
        uint32_t lo = 0xffffffffu, hi = 0;
        for (int l = it.l0; l < it.l0 + it.nl; l++) {
            const Leaf &lf = leaves[l];
            if (lf.kind == K_PTR || lf.kind == K_WORD || lf.kind == K_OV) return 0;
            if (lf.off < lo) lo = lf.off;
            if (lf.off + lf.n > hi) hi = lf.off + lf.n;
        }
        if (hi <= lo || !(mo.buf = (uint8_t *)malloc(hi - lo))) return 0;
        mo.lo = lo; mo.n = hi - lo; mo.st = 1;
    }
    return mo.st == 1 ? &mo : 0;
}

/* the items of one area (or one pool element): core items -> h (+ hf), full items -> hf only */
void feed_items(const Area &ar, uint64_t *h, uint64_t *hf_, FILE *df, const char *tag)
{
    for (int k = ar.i0; k < ar.i0 + ar.ni; k++) {
        const Item &it = items[k];
        if (it.aux || (it.full && !hf_) || (it.info && !df)) continue;
        if (it.wnc && !conds_true(it.wc0, it.wnc)) continue;   /* when= off: not hashed, not dumped */
        uint64_t *hh = it.full ? 0 : h;
        uint64_t *hf = it.info ? 0 : hf_;   /* info: dump only */
        const char *an = it.info ? "/info" : it.full ? "/full" : "";
        uint32_t base = 0;
        int r = item_base(it, &base);
        if (r != 1) {
            uint64_t v = r ? 0x756e7265616462ull : 0x6e756c6cull;
            if (hh) feed(*hh, v);
            if (hf) feed(*hf, v);
            if (df) fprintf(df, "%s%s %s%s = %s\n", ar.name, an, tag, it.path, r ? "unreadable" : "null");
            continue;
        }
        int l0 = it.l0, l1 = it.l0 + it.nl;
        if (it.bounded) {   /* elements at..at+n-1 of the array, n fed first */
            int ok = 1;
            long long n = opnd_val(it.n, &ok), at = it.has_at ? opnd_val(it.at, &ok) : 0;
            if (!ok) n = at = 0;
            if (at < 0) at = 0;
            if (at > (long long)it.ecnt) at = it.ecnt;
            if (n < 0) n = 0;
            if (n > (long long)it.ecnt - at) n = (long long)it.ecnt - at;
            int per = it.nl / (int)it.ecnt;
            l0 = it.l0 + (int)at * per;
            l1 = l0 + (int)n * per;
            if (hh) feed(*hh, (uint64_t)n);
            if (hf) feed(*hf, (uint64_t)n);
            if (df) fprintf(df, "%s%s %s%s(used) = %lld at %lld\n", ar.name, an, tag, it.path, n, at);
        }
        static char txt[65536];   /* K_WORD buffers (stCom.mng_buff: 1280 words) dump whole */
        Memo *mo = hf && !hh && !it.bounded && !it.rel && !it.nd ? memo_for(k, it) : 0;
        if (mo) {   /* one sub-hash, reused while the bytes are unchanged */
            const void *p = be->ptr(base + mo->lo, mo->n);
            if (p && mo->valid && !df && !memcmp(p, mo->buf, mo->n)) { feed(*hf, mo->h); continue; }
            uint64_t sub = 0x6d656d6full;
            for (int l = l0; l < l1; l++) {
                leaf_feed(leaves[l], base, 0, &sub, df ? txt : 0, sizeof txt);
                if (df) fprintf(df, "%s%s %s%s = %s\n", ar.name, an, tag, leaves[l].name, txt);
            }
            feed(*hf, sub);
            mo->valid = p != 0;
            if (p) { memcpy(mo->buf, p, mo->n); mo->h = sub; }
            continue;
        }
        for (int l = l0; l < l1; l++) {
            leaf_feed(leaves[l], base, hh, hf, df ? txt : 0, sizeof txt);
            if (df) fprintf(df, "%s%s %s%s = %s\n", ar.name, an, tag, leaves[l].name, txt);
        }
    }
}

int dump_this(uint32_t vb)
{
    for (int k = 0; k < ndump; k++) if (dump_list[k] == vb) return 1;
    return vb >= dump_a && vb <= dump_b;
}

void parse_dump(const char *s)
{
    if (!s || !*s) return;
    const char *dash = strchr(s, '-');
    if (dash && !strchr(s, ',')) { dump_a = (uint32_t)strtoul(s, 0, 0); dump_b = (uint32_t)strtoul(dash + 1, 0, 0); return; }
    for (const char *p = s; *p && ndump < 64;) {
        char *e;
        unsigned long v = strtoul(p, &e, 0);
        if (e == p) break;
        dump_list[ndump++] = (uint32_t)v;
        p = *e == ',' ? e + 1 : e;
    }
}

int init()
{
    be = sbx_backend();
    if (!be) { snprintf(err, sizeof err, "no backend"); return 0; }
    const char *e;
    if (!sbx_names_load(be, &e)) { snprintf(err, sizeof err, "%s", e); return 0; }
    const char *fp = getenv("ACNB_STATE_FIELDS");
    char path[800];
    if (fp && *fp) snprintf(path, sizeof path, "%s", fp);
    else {   /* hybrid: port\build -> port\statehash; source port: sourceport\build -> port\statehash */
        snprintf(path, sizeof path, "%s\\..\\statehash\\fields.txt", be->table_dir);
        FILE *t = fopen(path, "r");
        if (t) fclose(t);
        else snprintf(path, sizeof path, "%s\\..\\..\\port\\statehash\\fields.txt", be->table_dir);
    }
    const char *fo = getenv("ACNB_STATE_FULL");
    full_on = !(fo && !strcmp(fo, "0"));
    if (!load_fields(path)) return 0;
    const char *ty;
    has_frame = sbx_lookup(0, 0, "FRAME", &frame_addr, &ty, &e);
    parse_dump(getenv("ACNB_STATE_DUMP"));
    const char *dd = getenv("ACNB_STATE_DUMP_DIR");
    if (dd && *dd) dump_dir = dd;
    if (ndump || dump_b >= dump_a) _mkdir(dump_dir);   /* one level; an existing dir is fine (verifier s103) */
    int nfull = 0, nf = 0;
    for (int k = 0; k < nitems; k++) if (items[k].full) { nfull++; nf += items[k].nl; }
    char m[400];
    snprintf(m, sizeof m, "statehash: %d areas, %d pools, %d items, %d fields (%d full-tier items / %d fields, full %s) (%s)",
             nareas, npools, nitems, nleaves, nfull, nf, full_on ? "on" : "off", path);
    be->log(m);
    QueryPerformanceFrequency(&qpf);
    return 1;
}

}  // namespace

extern "C" int statehash_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("ACNB_STATE_HASH");
        on = v && *v && strcmp(v, "0");
    }
    return on;
}

extern "C" void statehash_vblank(uint32_t vb)
{
    if (state < 0) {
        state = init();
        if (!state) {
            if (be) log_line("statehash: ERROR %s (state hash off)", err);
            else fprintf(stderr, "statehash: ERROR %s (state hash off)\n", err);
        }
    }
    if (state != 1) return;
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    cur_ovl = be->overlay();
    pools_update();
    FILE *df = 0;
    if (dump_this(vb)) {
        char p[900];
        snprintf(p, sizeof p, "%s\\state_%u.txt", dump_dir, vb);
        df = fopen(p, "w");
    }
    uint64_t total = fnv64("total");
    char line[4096];
    uint32_t fr = 0;
    int fr_ok = has_frame && rd(frame_addr, 4, &fr);
    int o = fr_ok ? snprintf(line, sizeof line, "state vb %u fr %u ov %d: ", vb, fr, cur_ovl)
                  : snprintf(line, sizeof line, "state vb %u fr - ov %d: ", vb, cur_ovl);
    int hpos = o;
    o += snprintf(line + o, sizeof line - o, "%016x", 0);   /* placeholder, total filled in below */
    if (df) for (int k = 0; k < npools; k++) {
        const Pool &pl = pools[k];
        if (pl.on) fprintf(df, "# pool %s on (%u x 0x%x)\n", pl.name, pl.count, pl.esz);
        else fprintf(df, "# pool %s off\n", pl.name);
    }
    for (int a = 0; a < nareas; a++) {
        Area &ar = areas[a];
        ar.on = conds_true(ar.c0, ar.nc);
        uint64_t h = fnv64(ar.name), hf = fnv64(ar.name) ^ 0x66756c6cull;
        ar.live = 0;
        if (ar.on && ar.pool >= 0 && !pools[ar.pool].on) ar.on = 0;   /* pool not there: area off */
        if (df) fprintf(df, "# %s %s\n", ar.name, ar.on ? "on" : "off");
        if (ar.on && ar.pool < 0) feed_items(ar, &h, full_on ? &hf : 0, df, "");
        else if (ar.on) {   /* each element: core order-free (sum of element hashes), full in slot order */
            const Pool &pl = pools[ar.pool];
            uint64_t sum = 0;
            for (uint32_t k = (uint32_t)(ar.from > 0 ? ar.from : 0); k < pl.count; k++) {
                cur_elem = pl.base + k * pl.esz;
                if (!conds_true(ar.lc0, ar.lnc)) continue;
                uint64_t hk = fnv64(ar.name), hkf = hk;
                char tag[24];
                snprintf(tag, sizeof tag, "[%u]", k);
                feed_items(ar, &hk, full_on ? &hkf : 0, df, tag);
                if (ar.ordered) { feed(h, k); feed(h, hk); }
                else sum += fmix(hk);
                ar.live++;
                if (full_on) { feed(hf, k); feed(hf, hkf); }
            }
            feed(h, ar.live);
            if (!ar.ordered) feed(h, sum);
            feed(hf, ar.live);
        }
        ar.h = h;
        ar.hf = hf;
        feed(total, (uint64_t)ar.on);
        if (ar.on) feed(total, h);
        if (o < (int)sizeof line - 48) {
            uint32_t s = (uint32_t)(h ^ (h >> 32));
            if (!ar.on) o += snprintf(line + o, sizeof line - o, " %s -", ar.name);
            else if (ar.pool >= 0) o += snprintf(line + o, sizeof line - o, " %s %u:%08x", ar.name, ar.live, s);
            else o += snprintf(line + o, sizeof line - o, " %s %08x", ar.name, s);
        }
    }
    if (full_on) {
        uint64_t ft = fnv64("full");
        feed(ft, total);
        for (int a = 0; a < nareas; a++) {
            feed(ft, (uint64_t)areas[a].on);
            if (areas[a].on) feed(ft, areas[a].hf);
        }
        if (o < (int)sizeof line - 20) o += snprintf(line + o, sizeof line - o, " full %08x", (uint32_t)(ft ^ (ft >> 32)));
    } else if (o < (int)sizeof line - 10) o += snprintf(line + o, sizeof line - o, " full -");
    char th[20];
    snprintf(th, sizeof th, "%016llx", (unsigned long long)total);
    memcpy(line + hpos, th, 16);
    QueryPerformanceCounter(&t1);   /* the hash only: the line write (SP: unbuffered stderr) is not counted */
    be->log(line);
    if (df) { fprintf(df, "# %s\n", line); fclose(df); }   /* the state line (stcmp --dump skips '#' rows) */
    double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)qpf.QuadPart;
    if (!cost_n) cost_vb0 = vb;
    cost_sum += ms;
    if (ms > cost_max) cost_max = ms;
    if (++cost_n == 1000) {
        char m[200];
        snprintf(m, sizeof m, "statehash: cost vb %u-%u avg %.3f ms max %.3f ms per vblank (full %s)", cost_vb0, vb,
                 cost_sum / cost_n, cost_max, full_on ? "on" : "off");
        be->log(m);
        cost_n = 0; cost_sum = cost_max = 0;
    }
}
