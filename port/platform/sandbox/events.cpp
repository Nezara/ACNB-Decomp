/* Sandbox pokes, freezes and replay format v3, one copy in both builds.
 *
 * Event = one line of text, the same in the script run and in the replay:
 *   poke T EXPR BITS            write BITS (hex, T's size, little-endian) to EXPR now
 *   freeze ID T EXPR BITS       write it now and at every later poke point until `unfreeze ID`
 *   unfreeze ID | unfreeze all
 * T = s8 u8 s16 u16 s32 u32 s64 u64 f32 (f32: the float's bits). EXPR = a script expression (sandbox.h names; the
 * script's field paths are flattened to SYM+0xOFF at record time, pointer places stay expressions and are evaluated
 * again in the playing build, so heap addresses may differ between builds). A place that names a symbol of an overlay
 * that is not loaded is an error (a freeze skips that vblank, logged once). Stores go through the backend's wptr
 * (source port: committed PAGE_READWRITE pages only).
 * Poke point: sbx_vblank, once per vblank right after the pads are decided (the script's own pokes run inside its
 * poll at the same point; no game code runs between the two). Order per vblank: the events of that vblank in file
 * order (script: in command order), then every active freeze in creation order.
 *
 * Replay v3 (platform/replay.h):
 *   #acnb-replay 3
 *   #acnb-sim ..                                          (as v2)
 *   #acnb-sbx 1 script=NAME sha1=H card=NAME|- card_sha1=H ee_cpi=N iop_cpi0=N overlays=-
 *   #sbx-line LOC TEXT                                    (the expanded script: every raw line of the script and its
 *                                                          includes in load order; LOC = N or INCLUDE:N)
 *   pad / p2 / seg lines as v2, and "x V EVENT" lines
 * sha1 = SHA-1 over the #sbx-line payloads ("LOC TEXT\n" each); card_sha1 = SHA-1 over the card directory at the
 * first poke point (sorted relative paths: "D path\n" per directory, "F path size\n" + bytes per file; mtimes are not
 * part of it). Playback refuses (exit 2) a card with another hash and any overlay pin (disc overlays = S6). */
#include "sandbox.h"
#include "sandbox_int.h"
#include "../replay.h"
#include "../ps2_mc.h"
#include "../sha1.h"
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

namespace {

char errbuf[900];
const char *errf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const char *errf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(errbuf, sizeof errbuf, fmt, ap);
    va_end(ap);
    return errbuf;
}
void logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char *fmt, ...)
{
    char b[1600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    const SbxBackend *be = sbx_backend();
    if (be) be->log(b); else fprintf(stderr, "%s\n", b);
}

struct Line { char *loc, *text; };
Line *lines;
int nlines, cap_lines;

struct Hdr {
    int active, from_replay;
    char script[200], card[100], card_sha1[41], sha1[41];
} hdr = {0, 0, "-", "-", "", ""};

FILE *rec_f;          /* sbx_rec_start: the --record file (v3 header deferred to the first poke point) */
char *rec_sim;
int rec_has_sim, rec_pending, begun, fail_code, ev_i;

struct Freeze { int id; char type[8]; char *expr; uint64_t bits; int warned; };
Freeze frz[64];
int nfrz;

/* ---------------------------------------------------------------- hashes */

void lines_sha1(char out[41])
{
    Sha1 s;
    sha1_init(&s);
    for (int i = 0; i < nlines; i++) {
        sha1_update(&s, lines[i].loc, strlen(lines[i].loc));
        if (lines[i].text[0]) { sha1_update(&s, " ", 1); sha1_update(&s, lines[i].text, strlen(lines[i].text)); }
        sha1_update(&s, "\n", 1);
    }
    uint8_t d[20];
    sha1_final(&s, d);
    sha1_hex(d, out);
}

struct Ent { char name[260]; int dir; };
int ent_cmp(const void *a, const void *b) { return strcmp(((const Ent *)a)->name, ((const Ent *)b)->name); }

void hash_dir(Sha1 *s, const char *dir, const char *rel, int depth)
{
    char pat[1200];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    struct _finddata_t fd;
    intptr_t h = _findfirst(pat, &fd);
    if (h == -1) return;
    Ent *v = 0;
    int n = 0, cap = 0;
    do {
        if (!strcmp(fd.name, ".") || !strcmp(fd.name, "..")) continue;
        if (n == cap) v = (Ent *)realloc(v, (size_t)(cap = cap ? cap * 2 : 32) * sizeof *v);
        snprintf(v[n].name, sizeof v[n].name, "%s", fd.name);
        v[n++].dir = (fd.attrib & _A_SUBDIR) != 0;
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
    if (n) qsort(v, (size_t)n, sizeof *v, ent_cmp);
    for (int i = 0; i < n; i++) {
        char r[1200], p[1200], l[1300];
        snprintf(r, sizeof r, "%s%s%s", rel, rel[0] ? "/" : "", v[i].name);
        snprintf(p, sizeof p, "%s\\%s", dir, v[i].name);
        if (v[i].dir) {
            int k = snprintf(l, sizeof l, "D %s\n", r);
            sha1_update(s, l, (size_t)k);
            if (depth < 8) hash_dir(s, p, r, depth + 1);
            continue;
        }
        FILE *f = fopen(p, "rb");
        long sz = 0;
        if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET); }
        int k = snprintf(l, sizeof l, "F %s %ld\n", r, sz);
        sha1_update(s, l, (size_t)k);
        char buf[16384];
        size_t got;
        while (f && (got = fread(buf, 1, sizeof buf, f)) > 0) sha1_update(s, buf, got);
        if (f) fclose(f);
    }
    free(v);
}

void card_hash(const char *dir, char out[41])
{
    Sha1 s;
    sha1_init(&s);
    if (dir && *dir) hash_dir(&s, dir, "", 0);
    uint8_t d[20];
    sha1_final(&s, d);
    sha1_hex(d, out);
}

/* ---------------------------------------------------------------- stores */

int write_place(const char *type, const char *expr, uint64_t bits, uint32_t *at, const char **err)
{
    const SbxBackend *be = sbx_backend();
    uint32_t n = sbx_scalar_size(type);
    if (!n) { *err = errf("unknown poke type %s", type); return 0; }
    double v;
    int ovl = 0;
    if (!sbx_eval(expr, 0, &v, err, &ovl)) return 0;
    if (ovl < 0) { *err = errf("%s names symbols of both overlays", expr); return 0; }
    if (ovl && be->overlay() != ovl) {
        *err = errf("%s is in %s, which is not loaded now", expr, ovl == 2 ? "OLMENU" : "OLSORTIE");
        return 0;
    }
    uint32_t a = (uint32_t)(int64_t)v;
    void *h = be->wptr ? be->wptr(a, n) : 0;
    if (!h) { *err = errf("%s = 0x%x: %u bytes not writable in this build", expr, a, n); return 0; }
    memcpy(h, &bits, n);
    *at = a;
    return 1;
}

const char *skip_ws(const char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

/* "T EXPR BITS" -> parts (expr into ex) */
int split_tail(const char *s, char type[8], char *ex, size_t exn, uint64_t *bits, const char **err)
{
    s = skip_ws(s);
    int k = 0;
    while (s[k] && !isspace((unsigned char)s[k]) && k < 7) { type[k] = s[k]; k++; }
    type[k] = 0;
    if (!sbx_scalar_size(type)) { *err = errf("unknown poke type \"%s\"", type); return 0; }
    s = skip_ws(s + k);
    const char *last = strrchr(s, ' ');
    if (!last || last == s) { *err = "event needs T EXPR BITS"; return 0; }
    char *e;
    *bits = strtoull(last + 1, &e, 16);
    if (strncmp(last + 1, "0x", 2) || *e) { *err = errf("bad value bits \"%s\" (0x hex)", last + 1); return 0; }
    size_t n = (size_t)(last - s);
    while (n && isspace((unsigned char)s[n - 1])) n--;
    if (n >= exn) { *err = "place expression too long"; return 0; }
    memcpy(ex, s, n);
    ex[n] = 0;
    return 1;
}

int find_frz(int id)
{
    for (int i = 0; i < nfrz; i++) if (frz[i].id == id) return i;
    return -1;
}

void write_header(void)
{
    const SbxBackend *be = sbx_backend();
    int ee = 0, iop = 0;
    if (be && be->get_pins) be->get_pins(&ee, &iop);
    lines_sha1(hdr.sha1);
    replay_rec_header_v(rec_f, 3, rec_has_sim ? rec_sim : 0);
    fprintf(rec_f, "#acnb-sbx 1 script=%s sha1=%s card=%s card_sha1=%s ee_cpi=%d iop_cpi0=%d overlays=-\n", hdr.script,
            hdr.sha1, hdr.card, hdr.card_sha1, ee, iop);
    for (int i = 0; i < nlines; i++)
        fprintf(rec_f, "#sbx-line %s%s%s\n", lines[i].loc, lines[i].text[0] ? " " : "", lines[i].text);
    fflush(rec_f);
}

void reset_lines(void)
{
    for (int i = 0; i < nlines; i++) { free(lines[i].loc); free(lines[i].text); }
    nlines = 0;
}

}  // namespace

uint32_t sbx_scalar_size(const char *t)
{
    static const struct { const char *n; uint32_t s; } k[] = {
        {"s8", 1}, {"u8", 1}, {"s16", 2}, {"u16", 2}, {"s32", 4}, {"u32", 4}, {"s64", 8}, {"u64", 8}, {"f32", 4}};
    for (auto &x : k) if (!strcmp(x.n, t)) return x.s;
    return 0;
}

void sbx_hdr_line(const char *loc, const char *text)
{
    if (hdr.from_replay) return;   /* a v3 replay's own header wins over an observing script */
    if (nlines == cap_lines) lines = (Line *)realloc(lines, (size_t)(cap_lines = cap_lines ? cap_lines * 2 : 256) * sizeof *lines);
    lines[nlines].loc = strdup(loc);
    lines[nlines++].text = strdup(text);
}

void sbx_hdr_script(const char *name, const char *card, int sandbox)
{
    if (hdr.from_replay) return;
    snprintf(hdr.script, sizeof hdr.script, "%s", name);
    for (char *c = hdr.script; *c; c++) if (isspace((unsigned char)*c)) *c = '_';
    snprintf(hdr.card, sizeof hdr.card, "%s", card);
    hdr.active = sandbox;
}

extern "C" int sbx_rec_start(FILE *rec, const char *simcfg)
{
    if (!hdr.active || !rec) return 0;
    rec_f = rec;
    rec_has_sim = simcfg != 0;
    rec_sim = strdup(simcfg ? simcfg : "");
    rec_pending = 1;
    return 1;
}

const char *sbx_ev_begin(void)
{
    if (begun || !hdr.active) return 0;
    begun = 1;
    const SbxBackend *be = sbx_backend();
    char h[41];
    card_hash(ps2mc_card_dir(), h);
    const char *err = 0;
    if (hdr.from_replay) {
        if (strcmp(h, hdr.card_sha1))
            err = errf("the run's card (%s, sha1 %s) is not the replay's card %s (card_sha1 %s): start it with that card",
                       ps2mc_card_dir() ? ps2mc_card_dir() : "-", h, hdr.card, hdr.card_sha1);
    } else {
        snprintf(hdr.card_sha1, sizeof hdr.card_sha1, "%s", h);
        if (strcmp(hdr.card, "-")) {
            char ref[41], p[900];
            if (!strcmp(hdr.card, "empty")) card_hash(0, ref);
            else {
                snprintf(p, sizeof p, "%s\\%s_card_orig", be && be->build_dir ? be->build_dir : ".", hdr.card);
                if (_access(p, 0)) err = errf("card %s: no reference card %s", hdr.card, p);
                else card_hash(p, ref);
            }
            if (!err && strcmp(h, ref))
                err = errf("card %s: the run's card (sha1 %s) is not %s (sha1 %s): start the run with that card "
                           "(scriptrun -Card %s)", hdr.card, h, hdr.card, ref, hdr.card);
        }
    }
    logf("sandbox: card %s sha1 %s%s", hdr.card, h, err ? " (MISMATCH)" : "");
    if (rec_pending) { write_header(); rec_pending = 0; }
    return err;
}

int sbx_ev_freeze_active(int id) { return find_frz(id) >= 0; }
int sbx_ev_failed(void) { return fail_code; }

int sbx_ev_do(const char *text, uint32_t vb, int lint, FILE *rec, const char **err)
{
    char kw[16] = "";
    sscanf(text, "%15s", kw);
    const char *s = skip_ws(text + strlen(kw));
    char type[8], ex[1200];
    uint64_t bits = 0;
    if (!strcmp(kw, "poke") || !strcmp(kw, "freeze")) {
        int id = 0;
        if (kw[0] == 'f') {
            char *e;
            id = (int)strtol(s, &e, 10);
            if (e == s || id <= 0) { *err = "freeze needs an id >= 1"; return 0; }
            s = e;
        }
        if (!split_tail(s, type, ex, sizeof ex, &bits, err)) return 0;
        if (lint) {
            double v;
            int ovl = 0;
            return sbx_eval(ex, 1, &v, err, &ovl);
        }
        if (kw[0] == 'p') {
            uint32_t at;
            if (!write_place(type, ex, bits, &at, err)) return 0;
            logf("sandbox: @%u poke %s %s = 0x%llx (at 0x%x)", vb, type, ex, (unsigned long long)bits, at);
        } else {
            if (find_frz(id) >= 0) { *err = errf("freeze id %d is already active", id); return 0; }
            if (nfrz == 64) { *err = "more than 64 freezes"; return 0; }
            Freeze &f = frz[nfrz++];
            f.id = id;
            snprintf(f.type, sizeof f.type, "%s", type);
            f.expr = strdup(ex);
            f.bits = bits;
            f.warned = 0;
            logf("sandbox: @%u freeze %d %s %s = 0x%llx", vb, id, type, ex, (unsigned long long)bits);
        }
    } else if (!strcmp(kw, "unfreeze")) {
        int all = !strncmp(s, "all", 3) && !s[3];
        char *e;
        int id = all ? 0 : (int)strtol(s, &e, 10);
        if (!all && (e == s || *e || id <= 0)) { *err = "unfreeze ID|all"; return 0; }
        if (lint) return 1;
        if (all) {
            for (int i = 0; i < nfrz; i++) free(frz[i].expr);
            nfrz = 0;
        } else {
            int i = find_frz(id);
            if (i < 0) { *err = errf("unfreeze %d: no such freeze", id); return 0; }
            free(frz[i].expr);
            memmove(&frz[i], &frz[i + 1], (size_t)(nfrz - i - 1) * sizeof *frz);
            nfrz--;
        }
        logf("sandbox: @%u unfreeze %s", vb, s);
    } else {
        *err = errf("unknown event \"%s\"", kw);
        return 0;
    }
    if (rec) { fprintf(rec, "x %u %s\n", vb, text); fflush(rec); }
    return 1;
}

extern "C" int sbx_replay_load(const char *path)
{
    const SbxBackend *be = sbx_backend();
    if (!be) { fprintf(stderr, "sandbox: no backend\n"); return 0; }
    const char *h = replay_sbx_header();
    if (!h) { logf("sandbox: replay %s is format 3 but has no #acnb-sbx header: refused", path); return 0; }
    int ver = 0;
    if (sscanf(h, "#acnb-sbx %d", &ver) != 1 || ver != 1) { logf("sandbox: replay %s: #acnb-sbx version %d is unknown: refused", path, ver); return 0; }
    char script[200] = "", sha[64] = "", card[100] = "", csha[64] = "", ovl[200] = "";
    int ee = -1, iop = -1;
    for (const char *p = strchr(h + 10, ' '); p && *p;) {
        p = skip_ws(p);
        char tok[300];
        int k = 0;
        while (p[k] && p[k] != ' ' && k < 299) { tok[k] = p[k]; k++; }
        tok[k] = 0;
        p += k;
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        const char *v = eq + 1;
        if (!strcmp(tok, "script")) snprintf(script, sizeof script, "%s", v);
        else if (!strcmp(tok, "sha1")) snprintf(sha, sizeof sha, "%s", v);
        else if (!strcmp(tok, "card")) snprintf(card, sizeof card, "%s", v);
        else if (!strcmp(tok, "card_sha1")) snprintf(csha, sizeof csha, "%s", v);
        else if (!strcmp(tok, "ee_cpi")) ee = atoi(v);
        else if (!strcmp(tok, "iop_cpi0")) iop = atoi(v);
        else if (!strcmp(tok, "overlays")) snprintf(ovl, sizeof ovl, "%s", v);
        else logf("sandbox: replay header key %s is unknown (ignored)", tok);
    }
    if (!script[0] || strlen(csha) != 40 || !ovl[0] || ee < 0 || iop < 0) {
        logf("sandbox: replay %s: incomplete #acnb-sbx header (script, card_sha1, ee_cpi, iop_cpi0, overlays): refused", path);
        return 0;
    }
    if (strcmp(ovl, "-")) { logf("sandbox: replay %s pins disc overlays (%s): this build cannot serve them yet: refused", path, ovl); return 0; }
    const char *err = 0;
    if (!sbx_engine_init(&err)) { logf("sandbox: replay %s: %s: refused", path, err); return 0; }
    /* the embedded script (header record / re-record) */
    reset_lines();
    hdr.from_replay = 1;
    FILE *f = fopen(path, "r");
    char line[4096];
    while (f && fgets(line, sizeof line, f)) {
        if (strncmp(line, "#sbx-line ", 10)) continue;
        line[strcspn(line, "\r\n")] = 0;
        char *loc = line + 10, *sp = strchr(loc, ' ');
        if (sp) *sp = 0;
        if (nlines == cap_lines) lines = (Line *)realloc(lines, (size_t)(cap_lines = cap_lines ? cap_lines * 2 : 256) * sizeof *lines);
        lines[nlines].loc = strdup(loc);
        lines[nlines++].text = strdup(sp ? sp + 1 : "");
    }
    if (f) fclose(f);
    char mine[41];
    lines_sha1(mine);
    if (strcmp(mine, sha)) logf("sandbox: replay %s: the embedded script's sha1 %s differs from the header's %s (edited?)", path, mine, sha);
    hdr.active = 1;
    snprintf(hdr.script, sizeof hdr.script, "%s", script);
    snprintf(hdr.card, sizeof hdr.card, "%s", card[0] ? card : "-");
    snprintf(hdr.card_sha1, sizeof hdr.card_sha1, "%s", csha);
    if (be->set_pins) be->set_pins(ee, iop);
    int n = replay_nevents();
    for (int i = 0; i < n; i++) {
        uint32_t vb;
        const char *t;
        int ln;
        replay_event(i, &vb, &t, &ln);
        if (!sbx_ev_do(t, vb, 1, 0, &err)) { logf("sandbox: replay %s line %d (x %u %s): %s: refused", path, ln, vb, t, err); return 0; }
    }
    logf("sandbox: SANDBOX replay %s (script %s, %d events, card %s)", path, script, n, hdr.card);
    return 1;
}

extern "C" void sbx_vblank(uint32_t vb, uint32_t *exit_after)
{
    if (!hdr.active && !nfrz) return;
    const char *e = sbx_ev_begin();
    if (e) {
        logf("sandbox: %s at vblank %u: %s", hdr.from_replay ? "replay refused" : "FAIL", vb, e);
        if (!fail_code) fail_code = hdr.from_replay ? 2 : 3;
        if (!*exit_after) *exit_after = vb + 1;
    }
    if (hdr.from_replay && !fail_code) {
        uint32_t ev;
        const char *t;
        int ln;
        while (replay_event(ev_i, &ev, &t, &ln) && ev <= vb) {
            ev_i++;
            const char *err;
            if (!sbx_ev_do(t, vb, 0, rec_f, &err)) {
                logf("sandbox: FAIL replay line %d (x %u %s) at vblank %u: %s", ln, ev, t, vb, err);
                fail_code = 3;
                if (!*exit_after) *exit_after = vb + 1;
                break;
            }
        }
    }
    for (int i = 0; i < nfrz; i++) {
        const char *err;
        uint32_t at;
        if (write_place(frz[i].type, frz[i].expr, frz[i].bits, &at, &err)) { frz[i].warned = 0; continue; }
        if (!frz[i].warned) logf("sandbox: @%u freeze %d skipped while: %s", vb, frz[i].id, err);
        frz[i].warned = 1;
    }
}
