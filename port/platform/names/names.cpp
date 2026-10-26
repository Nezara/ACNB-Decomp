/* Name + type tables (names.h; moved here from platform/sandbox in s103, shared by the sandbox and the state hash):
 * sbx_names.tsv / sbx_types.tsv from scripts/sbx_names.py, read once at the first sandbox / statehash load. Resolution is the same in both builds (same rows, same clash rule); only the address column differs
 * (backend row_addr). Plain C data (the hybrid's game.dll links without the C++ library). */
#include "names.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sbx_absent;
int sbx_last_ovl;
const SbxBackend *sbx_be;

void sbx_set_backend(const SbxBackend *b) { sbx_be = b; }
const SbxBackend *sbx_backend(void) { return sbx_be; }

namespace {

struct Row { const char *name, *file, *type; uint32_t ps2, size, rva, next_name, next_ps2; int has_rva; char kind; char ovl; char ovl_dash; };
struct Member { const char *name, *type; uint32_t off; };
struct Agg { const char *name; uint32_t size, first, count, next; int is_enum; };

/* chained hash buckets: index + 1 into rows / aggs, 0 = end */
enum { NB = 1 << 14 };
Row *rows;
int nrows;
uint32_t name_head[NB], ps2_head[NB];
Agg *aggs;
int naggs, cap_aggs;
uint32_t agg_head[NB / 4];
Member *mems;
int nmems, cap_mems;
/* this build's function address -> row (objs listing): open addressing */
struct AddrSlot { uint32_t a; int row; };
AddrSlot *addr_tab;
uint32_t addr_cap;

const SbxBackend *be;
int ready;
char errbuf[700];

uint32_t hs(const char *s, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
    return h;
}
uint32_t hs(const char *s) { return hs(s, strlen(s)); }
uint32_t hu(uint32_t a) { return (a * 2654435761u) >> 8; }

const char *errf(const char *fmt, const char *a, const char *b = "")
{
    snprintf(errbuf, sizeof errbuf, fmt, a, b);
    return errbuf;
}

/* split a tab line in place */
int split(char *s, char **f, int max)
{
    int n = 0;
    s[strcspn(s, "\r\n")] = 0;
    while (n < max) {
        f[n++] = s;
        char *t = strchr(s, '\t');
        if (!t) break;
        *t = 0;
        s = t + 1;
    }
    return n;
}

bool same_obj(const Row &a, const Row &b) { return a.ps2 == b.ps2 && !strcmp(a.file, b.file); }

bool file_match(const char *file, const char *q, size_t qn)
{
    size_t fn = strlen(file);
    if (fn == qn && !memcmp(file, q, qn)) return true;
    return fn > qn && file[fn - qn - 1] == '/' && !memcmp(file + fn - qn, q, qn);
}

/* candidates -> one object or an error */
int pick(const int *c, int nc, const char *what, uint32_t *addr, const char **type, const char **err)
{
    sbx_absent = 0;
    if (!nc) { *err = errf("unknown name %s", what); return 0; }
    for (int i = 1; i < nc; i++)
        if (!same_obj(rows[c[0]], rows[c[i]])) {
            int o = snprintf(errbuf, sizeof errbuf, "name clash %s (write FILE.c:NAME or @0xADDR):", what);
            for (int k = 0; k < nc && o < (int)sizeof errbuf - 1; k++)
                o += snprintf(errbuf + o, sizeof errbuf - o, " %s:%s@0x%x", rows[c[k]].file, rows[c[k]].name, rows[c[k]].ps2);
            *err = errbuf;
            return 0;
        }
    const Row &r = rows[c[0]];
    uint32_t a = be->row_addr(r.ps2, r.rva, r.has_rva);
    if (!a) { sbx_absent = 1; *err = errf("%s is not in the %s build (not native there yet)", what, be->build); return 0; }
    *addr = a;
    if (type) *type = r.type;
    sbx_last_ovl = r.ovl;
    return 1;
}

const Agg *find_agg(const char *name)
{
    for (uint32_t i = agg_head[hs(name) % (NB / 4)]; i; i = aggs[i - 1].next)
        if (!strcmp(aggs[i - 1].name, name)) return &aggs[i - 1];
    return 0;
}

char *dup(const char *s) { return strdup(s); }

}  // namespace

int sbx_names_load(const SbxBackend *b, const char **err)
{
    be = b;
    if (ready) return 1;
    char path[700], line[1200];
    snprintf(path, sizeof path, "%s\\sbx_names.tsv", b->table_dir);
    FILE *f = fopen(path, "r");
    if (!f) { *err = errf("no name table %s (the build writes it: scripts/sbx_names.py)", path); return 0; }
    char build[32] = "", sent[600] = "";
    int want_rows = 0;
    if (!fgets(line, sizeof line, f) || sscanf(line, "#sbx-names 1 %31s rows=%d sentinels=%599s", build, &want_rows, sent) != 3
        || want_rows <= 0) {
        fclose(f);
        *err = errf("%s: bad header (regenerate with scripts/sbx_names.py)", path);
        return 0;
    }
    if (strcmp(build, b->build)) { fclose(f); *err = errf("%s is the %s table, not this build's", path, build); return 0; }
    rows = (Row *)calloc((size_t)want_rows, sizeof *rows);
    while (rows && nrows < want_rows && fgets(line, sizeof line, f)) {
        char *c[8];
        if (split(line, c, 8) != 8) continue;
        Row &r = rows[nrows++];
        r.name = dup(c[0]); r.file = dup(c[1]); r.ps2 = (uint32_t)strtoul(c[2], 0, 16); r.kind = c[4][0];
        r.ovl = !strcmp(c[3], "OLMENU") ? 2 : !strcmp(c[3], "OLSORTIE") ? 1 : 0;
        r.ovl_dash = !strcmp(c[3], "-");   /* overlay address, overlay unknown (statehash: not an identity) */
        r.size = (uint32_t)strtoul(c[5], 0, 16); r.type = dup(c[6]);
        r.has_rva = c[7][0] != '-';
        r.rva = r.has_rva ? (uint32_t)strtoul(c[7], 0, 16) : 0;
    }
    fclose(f);
    if (nrows != want_rows) { *err = errf("%s: truncated (regenerate with scripts/sbx_names.py)", path); nrows = 0; return 0; }
    for (char *s = strtok(sent, ","); s; s = strtok(0, ",")) {   /* NAME:HEX, HEX in the table's address column */
        char *colon = strrchr(s, ':');
        if (!colon) continue;
        *colon = 0;
        uint32_t want = (uint32_t)strtoul(colon + 1, 0, 16), have = b->sentinel(s);
        uint32_t want_addr = !strcmp(build, "sourceport") ? b->row_addr(0, want, 1) : want;
        if (!have || have != want_addr) {
            snprintf(errbuf, sizeof errbuf, "%s is stale: sentinel %s at 0x%x in the table, 0x%x in this build (rebuild / rerun "
                     "scripts/sbx_names.py)", path, s, want_addr, have);
            *err = errbuf;
            nrows = 0;
            return 0;
        }
    }
    /* buckets: insert in reverse so a chain lists rows in table order */
    addr_cap = 1;
    while (addr_cap < (uint32_t)nrows * 2) addr_cap <<= 1;
    addr_tab = (AddrSlot *)calloc(addr_cap, sizeof *addr_tab);
    for (int i = nrows - 1; i >= 0; i--) {
        uint32_t hn = hs(rows[i].name) % NB, hp = hu(rows[i].ps2) % NB;
        rows[i].next_name = name_head[hn]; name_head[hn] = (uint32_t)i + 1;
        rows[i].next_ps2 = ps2_head[hp]; ps2_head[hp] = (uint32_t)i + 1;
    }
    for (int i = 0; i < nrows; i++) {
        if (rows[i].kind != 'f') continue;
        uint32_t a = b->row_addr(rows[i].ps2, rows[i].rva, rows[i].has_rva);
        if (!a) continue;
        for (uint32_t k = hu(a) & (addr_cap - 1);; k = (k + 1) & (addr_cap - 1)) {
            if (!addr_tab[k].a) { addr_tab[k].a = a; addr_tab[k].row = i; break; }
            if (addr_tab[k].a == a) break;   /* first row wins */
        }
    }
    snprintf(path, sizeof path, "%s\\sbx_types.tsv", b->table_dir);
    f = fopen(path, "r");
    if (!f) { *err = errf("no type table %s (the build writes it: scripts/sbx_names.py)", path); nrows = 0; return 0; }
    Agg *cur = 0;
    while (fgets(line, sizeof line, f)) {
        char *c[5];
        int n = split(line, c, 5);
        if ((c[0][0] == 'S' || c[0][0] == 'E') && n >= 3) {
            if (naggs == cap_aggs) aggs = (Agg *)realloc(aggs, (size_t)(cap_aggs = cap_aggs ? cap_aggs * 2 : 2048) * sizeof *aggs);
            cur = &aggs[naggs++];
            cur->name = dup(c[1]);
            cur->size = (uint32_t)strtoul(c[2], 0, 10);
            cur->first = (uint32_t)nmems;
            cur->count = 0;
            cur->is_enum = c[0][0] == 'E';
            uint32_t h = hs(cur->name) % (NB / 4);
            cur->next = agg_head[h];
            agg_head[h] = (uint32_t)naggs;
            if (cur->is_enum) cur = 0;
        } else if (c[0][0] == 'M' && n >= 4 && cur) {
            if (nmems == cap_mems) mems = (Member *)realloc(mems, (size_t)(cap_mems = cap_mems ? cap_mems * 2 : 8192) * sizeof *mems);
            char bt[200];   /* a bitfield member (c[4] = SIZE:OFFSET) gets type "bf:SIZE:OFFSET:T": pokes refuse it */
            if (n >= 5 && strcmp(c[4], "-")) snprintf(bt, sizeof bt, "bf:%s:%s", c[4], c[3]);
            mems[nmems++] = Member{dup(c[1]), dup(n >= 5 && strcmp(c[4], "-") ? bt : c[3]), (uint32_t)strtoul(c[2], 0, 10)};
            cur->count++;
        }
    }
    fclose(f);
    ready = 1;
    snprintf(errbuf, sizeof errbuf, "sandbox: names %s (%d rows, %d types, sentinels ok)", b->table_dir, nrows, naggs);
    b->log(errbuf);
    return 1;
}

int sbx_lookup(const char *qual, size_t qn, const char *name, uint32_t *addr, const char **type, const char **err)
{
    if (!ready) { *err = "no name table"; return 0; }
    int c[64], nc = 0;
    for (uint32_t i = name_head[hs(name) % NB]; i && nc < 64; i = rows[i - 1].next_name)
        if (!strcmp(rows[i - 1].name, name) && (!qual || file_match(rows[i - 1].file, qual, qn))) c[nc++] = (int)i - 1;
    if (qual && !nc) {
        char q[300];
        snprintf(q, sizeof q, "%.*s", (int)qn, qual);
        *err = errf("unknown name %s:%s", q, name);
        return 0;
    }
    return pick(c, nc, name, addr, type, err);
}

int sbx_lookup_ps2(uint32_t ps2, uint32_t *addr, const char **type, const char **err)
{
    if (!ready) { *err = "no name table"; return 0; }
    char what[32];
    snprintf(what, sizeof what, "@0x%x", ps2);
    int c[64], nc = 0;
    for (uint32_t i = ps2_head[hu(ps2) % NB]; i && nc < 64; i = rows[i - 1].next_ps2)
        if (rows[i - 1].ps2 == ps2) c[nc++] = (int)i - 1;
    if (!nc) { *err = errf("no symbol at PS2 address %s", what); return 0; }
    return pick(c, nc, what, addr, type, err);
}

const char *sbx_fn_name_at(uint32_t a)
{
    if (!addr_tab || !a) return 0;
    for (uint32_t k = hu(a) & (addr_cap - 1); addr_tab[k].a; k = (k + 1) & (addr_cap - 1))
        if (addr_tab[k].a == a) return rows[addr_tab[k].row].name;
    return 0;
}

uint32_t sbx_type_size(const char *t)
{
    static const struct { const char *n; uint32_t s; } fund[] = {
        {"s8", 1}, {"u8", 1}, {"s16", 2}, {"u16", 2}, {"s32", 4}, {"u32", 4}, {"s64", 8}, {"u64", 8}, {"f32", 4},
        {"f64", 8}, {"u128", 16}, {"p", 4}, {"v", 0}, {"f", 0}};
    if (!t) return 0;
    if (t[0] == 'a' && t[1] >= '0' && t[1] <= '9') {
        char *e;
        unsigned long n = strtoul(t + 1, &e, 10);
        return *e == ':' ? (uint32_t)n * sbx_type_size(e + 1) : 0;
    }
    if ((t[0] == 's' || t[0] == 'e') && t[1] == ':') {
        const Agg *g = find_agg(t + 2);
        return g ? g->size : t[0] == 'e' ? 4 : 0;
    }
    if (t[0] == 'x') return (uint32_t)atoi(t + 1);
    for (auto &f : fund) if (!strcmp(f.n, t)) return f.s;
    return 0;
}

namespace {

int elem(const char *t, uint32_t *count, const char **et)
{
    if (!t || t[0] != 'a' || t[1] < '0' || t[1] > '9') return 0;
    char *e;
    *count = (uint32_t)strtoul(t + 1, &e, 10);
    if (*e != ':') return 0;
    *et = e + 1;
    return 1;
}

int member(const char *t, const char *m, size_t mn, uint32_t *off, const char **mt)
{
    if (!t || t[0] != 's' || t[1] != ':') return 0;
    const Agg *g = find_agg(t + 2);
    if (!g || g->is_enum) return 0;
    for (uint32_t i = g->first; i < g->first + g->count; i++)
        if (strlen(mems[i].name) == mn && !memcmp(mems[i].name, m, mn)) { *off = mems[i].off; *mt = mems[i].type; return 1; }
    return 0;
}

int idc(int c) { return c == '_' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

}  // namespace

/* walks "[i]" / ".m" steps after a symbol; *p advanced past the path */
int sbx_walk(const char **p, uint32_t *addr, const char **type, const char **err, long (*index)(const char **, const char **))
{
    const char *s = *p;
    while (*s == '[' || (*s == '.' && idc(s[1]) && !(s[1] >= '0' && s[1] <= '9'))) {
        if (!*type || !**type || !strcmp(*type, "-")) { *err = "field path on a symbol without a DWARF type"; return 0; }
        if (*s == '[') {
            s++;
            uint32_t n;
            const char *et;
            if (!elem(*type, &n, &et)) { *err = errf("[..] on a non-array (type %s)", *type); return 0; }
            const char *ie = 0;
            long i = index(&s, &ie);
            if (ie) { *err = ie; return 0; }
            if (*s != ']') { *err = "missing ]"; return 0; }
            s++;
            if (i < 0 || (uint32_t)i >= n) { char b[24]; snprintf(b, sizeof b, "%ld", i); *err = errf("index %s out of range (%s)", b, *type); return 0; }
            *addr += (uint32_t)i * sbx_type_size(et);
            *type = et;
        } else {
            s++;
            const char *m = s;
            while (idc(*s)) s++;
            uint32_t off;
            const char *mt;
            if (!member(*type, m, (size_t)(s - m), &off, &mt)) {
                char mm[200];
                snprintf(mm, sizeof mm, "%.*s", (int)(s - m), m);
                *err = errf("no member %s in %s (a pointer is chased with u32(..) + offset)", mm, *type);
                return 0;
            }
            *addr += off;
            *type = mt;
        }
    }
    *p = s;
    return 1;
}

namespace {
long int_index(const char **s, const char **err)
{
    char *e;
    long v = strtol(*s, &e, 0);
    if (e == *s) *err = "index must be an integer here";
    *s = e;
    return v;
}
}  // namespace

extern "C" int sbx_names_ready(void) { return ready; }

extern "C" int sbx_resolve(const char *name, uint32_t *addr, const char **err)
{
    const char *t;
    if (name[0] == '@') return sbx_lookup_ps2((uint32_t)strtoul(name + 1, 0, 0), addr, &t, err);
    const char *colon = strchr(name, ':');
    return colon ? sbx_lookup(name, (size_t)(colon - name), colon + 1, addr, &t, err) : sbx_lookup(0, 0, name, addr, &t, err);
}

extern "C" int sbx_place(const char *path, uint32_t *addr, const char **type, const char **err)
{
    char id[300];
    const char *colon = strchr(path, ':'), *bracket = strchr(path, '[');
    size_t n = colon && (!bracket || colon < bracket) ? (size_t)(colon - path) + 1 + strcspn(colon + 1, "[.")
                                                      : strcspn(path, "[.");
    if (path[0] == '@') n = 1 + strcspn(path + 1, "[.");
    if (n >= sizeof id) { *err = "name too long"; return 0; }
    memcpy(id, path, n);
    id[n] = 0;
    const char *t = 0;
    char *c = strchr(id, ':');
    int ok = id[0] == '@' ? sbx_lookup_ps2((uint32_t)strtoul(id + 1, 0, 0), addr, &t, err)
             : c ? sbx_lookup(id, (size_t)(c - id), c + 1, addr, &t, err) : sbx_lookup(0, 0, id, addr, &t, err);
    if (!ok) return 0;
    const char *p = path + n;
    if (!sbx_walk(&p, addr, &t, err, int_index)) return 0;
    if (*p) { *err = "junk after field path"; return 0; }
    *type = t;
    return 1;
}

/* ---- statehash (s103): address -> symbol over all rows, struct member access ---- */
namespace {
struct SymSpan { uint32_t a, end; int row; };
SymSpan *spans[3];
int nspans[3];
int span_cmp(const void *x, const void *y)
{
    const SymSpan *p = (const SymSpan *)x, *q = (const SymSpan *)y;
    if (p->a != q->a) return p->a < q->a ? -1 : 1;
    return p->row < q->row ? -1 : p->row > q->row;
}
void build_spans()
{
    for (int k = 0; k < 3; k++) spans[k] = (SymSpan *)calloc((size_t)nrows + 1, sizeof(SymSpan));
    for (int i = 0; i < nrows; i++) {
        const Row &r = rows[i];
        if (r.ovl_dash) continue;
        uint32_t a = be->row_addr(r.ps2, r.rva, r.has_rva);
        if (!a) continue;
        uint32_t sz = r.size ? r.size : sbx_type_size(r.type);
        int k = r.ovl;
        spans[k][nspans[k]++] = SymSpan{a, a + (sz ? sz : 1), i};
    }
    for (int k = 0; k < 3; k++) qsort(spans[k], (size_t)nspans[k], sizeof(SymSpan), span_cmp);
    /* functions: the row size is the PS2 size, but in the source port the address is the host one (different code
     * size), so a PS2 size can run over the next host function (s103: ac2leg_Control read as mapatrb_GetInfo+0x20).
     * Clamp a function span to the next distinct start; a no-op for the hybrid's exact PS2 sizes. */
    for (int k = 0; k < 3; k++)
        for (int m = 0, n = nspans[k]; m < n; m++) {
            if (rows[spans[k][m].row].kind != 'f') continue;
            int j = m + 1;
            while (j < n && spans[k][j].a == spans[k][m].a) j++;
            if (j < n && spans[k][j].a < spans[k][m].end) spans[k][m].end = spans[k][j].a;
        }
}
int span_find(int k, uint32_t a)
{
    int lo = 0, hi = nspans[k] - 1, best = -1;
    while (lo <= hi) {   /* last span with start <= a */
        int m = (lo + hi) / 2;
        if (spans[k][m].a <= a) { best = m; lo = m + 1; } else hi = m - 1;
    }
    if (best < 0) return -1;
    uint32_t s = spans[k][best].a;
    while (best > 0 && spans[k][best - 1].a == s) best--;   /* same address: first row in table order */
    for (int m = best; m < nspans[k] && spans[k][m].a == s; m++)
        if (a < spans[k][m].end) return m;
    return -1;
}
/* a data row of table k whose range holds a, nearest start first (function rows of that table may start inside
 * it: ComData[4] at 0x1e8fd00 spans OLMENU code addresses in the hybrid table); -1 = none */
int span_find_data(int k, uint32_t a)
{
    int lo = 0, hi = nspans[k] - 1, best = -1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (spans[k][m].a <= a) { best = m; lo = m + 1; } else hi = m - 1;
    }
    for (int m = best, n = 0; m >= 0 && n < 256; m--, n++)
        if (rows[spans[k][m].row].kind == 'd' && a < spans[k][m].end) return m;
    return -1;
}
}  // namespace

extern "C" const char *sbx_sym_fold = 0;
extern "C" const char *sbx_sym_at(uint32_t a, int ovl, uint32_t *off)
{
    if (!ready || !a) return 0;
    if (!spans[0]) build_spans();
    int m = span_find(0, a), k = 0;
    if (ovl == 1 || ovl == 2) {
        /* the source port has main and overlay code in one host image, interleaved, so a main span (PS2 size) can
         * cover an overlay function: take the containing span that starts nearest below a (hybrid: the tables do
         * not overlap, same result as main first) */
        int mo = span_find(ovl, a);
        if (mo >= 0 && (m < 0 || spans[ovl][mo].a > spans[0][m].a)) { k = ovl; m = mo; }
        /* nothing in main / the loaded overlay: the other overlay's data rows. Menu-owned data the sortie keeps
         * using (ComData[4] is OLMENU data above OLSORTIE's end on the PS2; com_info[i].datap / opmng datap point
         * into it during the battle, s103 ST2 verify). A wrong hybrid name shows as a hybrid vs SP difference. */
        if (m < 0) {
            int o = 3 - ovl;
            mo = span_find_data(o, a);
            if (mo >= 0) { k = o; m = mo; }
        }
    }
    if (m < 0) return 0;
    *off = a - spans[k][m].a;
    sbx_sym_fold = 0;
    if (rows[spans[k][m].row].kind == 'f')   /* another function row at the same address (folded code) */
        for (int j = m - 1; j <= m + 1 && !sbx_sym_fold; j += 2)
            if (j >= 0 && j < nspans[k] && spans[k][j].a == spans[k][m].a && rows[spans[k][j].row].kind == 'f')
                sbx_sym_fold = rows[spans[k][j].row].name;
    return rows[spans[k][m].row].name;
}

extern "C" int sbx_agg(const char *name, uint32_t *first, uint32_t *count, uint32_t *size, int *is_enum)
{
    const Agg *g = find_agg(name);
    if (!g) return 0;
    *first = g->first; *count = g->is_enum ? 0 : g->count; *size = g->size; *is_enum = g->is_enum;
    return 1;
}

extern "C" void sbx_member(uint32_t i, const char **name, uint32_t *off, const char **type)
{
    *name = mems[i].name; *off = mems[i].off; *type = mems[i].type;
}
