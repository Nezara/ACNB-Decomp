/* Host disc backend: ISO image (reference) or extracted folder (virtual LSNs). See disc.h. */
#include "disc.h"
#include "plat.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define disc_fseek _fseeki64
#else
#define disc_fseek fseeko
#endif

/* one directory record: ISO = as on the disc ("" = self, "\1" = parent, files "NAME.EXT;1"); folder = the same shape */
struct Rec { char *name; uint32_t lsn, size; };
struct Dir { uint32_t lsn, size; Rec *rec; int nrec, cap; };
struct FFile { char *path; uint32_t lsn, nsec, size; };   /* folder source: one contiguous extent per file */

static int kind = DISC_NONE;
static FILE *iso;
static uint32_t root_lsn, root_size;
static int root_ok;
static Dir *dirs;
static int ndirs, capdirs;
static FFile *files;
static int nfiles, capfiles;
static uint32_t folder_end;      /* first LSN past the folder layout */
static FILE *ff_cur;             /* folder source: last opened file */
static int ff_cur_i = -1;

static void *grow(void *p, int *cap, int need, size_t esz)
{
    if (need <= *cap) return p;
    int n = *cap ? *cap * 2 : 16;
    while (n < need) n *= 2;
    p = realloc(p, (size_t)n * esz);
    if (!p) { plat_log("disc: out of memory"); abort(); }
    *cap = n;
    return p;
}

static char *dupstr(const char *s, size_t n)
{
    char *d = (char *)malloc(n + 1);
    if (!d) { plat_log("disc: out of memory"); abort(); }
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

static Dir *new_dir(uint32_t lsn, uint32_t size)
{
    dirs = (Dir *)grow(dirs, &capdirs, ndirs + 1, sizeof *dirs);
    Dir *d = &dirs[ndirs++];
    memset(d, 0, sizeof *d);
    d->lsn = lsn;
    d->size = size;
    return d;
}

static void add_rec(Dir *d, const char *name, size_t ln, uint32_t lsn, uint32_t size)
{
    d->rec = (Rec *)grow(d->rec, &d->cap, d->nrec + 1, sizeof *d->rec);
    Rec *r = &d->rec[d->nrec++];
    r->name = dupstr(name, strnlen(name, ln));   /* names compare as C strings: "." (one 0 byte) = "" */
    r->lsn = lsn;
    r->size = size;
}

void disc_close(void)
{
    for (int i = 0; i < ndirs; i++) {
        for (int k = 0; k < dirs[i].nrec; k++) free(dirs[i].rec[k].name);
        free(dirs[i].rec);
    }
    for (int i = 0; i < nfiles; i++) free(files[i].path);
    free(dirs);
    free(files);
    dirs = 0; files = 0;
    ndirs = capdirs = nfiles = capfiles = 0;
    if (iso) fclose(iso);
    if (ff_cur) fclose(ff_cur);
    iso = ff_cur = 0;
    ff_cur_i = -1;
    root_ok = 0;
    kind = DISC_NONE;
}

int disc_kind(void) { return kind; }

/* ---------------------------------------------------------------- ISO image */

static int iso_read(uint32_t lsn, uint32_t n, void *dst)
{
    if (!iso) return -1;
    disc_fseek(iso, (long long)lsn * DISC_SECTOR, SEEK_SET);
    return fread(dst, DISC_SECTOR, n, iso) == n ? 0 : -1;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* Parse a directory extent into records, in disc order. A sector that cannot be read ends the list (the old per-call
 * walk returned "not found" there, which is the same as the records simply ending). */
static Dir *iso_parse_dir(uint32_t lsn, uint32_t size)
{
    Dir *d = new_dir(lsn, size);
    uint8_t sec[DISC_SECTOR];
    for (uint32_t off = 0; off < size; off += DISC_SECTOR) {
        if (iso_read(lsn + off / DISC_SECTOR, 1, sec)) break;
        for (uint32_t i = 0; i + 33 <= DISC_SECTOR && sec[i];) {
            const uint8_t *r = sec + i;
            uint32_t ln = r[32];
            if (i + 33 + ln > DISC_SECTOR) break;
            add_rec(d, (const char *)r + 33, ln, rd32(r + 2), rd32(r + 10));
            i += r[0];
        }
    }
    return d;
}

static Dir *get_dir(uint32_t lsn, uint32_t size)
{
    for (int i = 0; i < ndirs; i++)
        if (dirs[i].lsn == lsn && dirs[i].size == size) return &dirs[i];
    return kind == DISC_ISO ? iso_parse_dir(lsn, size) : 0;
}

/* parse the whole tree once at open (directories = file flag bit 1; self / parent records skipped) */
static void iso_walk(uint32_t lsn, uint32_t size, int depth)
{
    if (depth > 16) return;
    for (int i = 0; i < ndirs; i++)
        if (dirs[i].lsn == lsn && dirs[i].size == size) return;
    get_dir(lsn, size);
    uint8_t sec[DISC_SECTOR];
    /* re-read flags: records keep only name / lsn / size, so find the subdirectories from the sectors again */
    for (uint32_t off = 0; off < size; off += DISC_SECTOR) {
        if (iso_read(lsn + off / DISC_SECTOR, 1, sec)) break;
        for (uint32_t i = 0; i + 33 <= DISC_SECTOR && sec[i];) {
            const uint8_t *r = sec + i;
            if ((r[25] & 2) && !(r[32] == 1 && r[33] <= 1)) iso_walk(rd32(r + 2), rd32(r + 10), depth + 1);
            i += r[0];
        }
    }
}

static int iso_open(const char *path)
{
    iso = fopen(path, "rb");
    if (!iso) return -1;
    kind = DISC_ISO;
    uint8_t pvd[DISC_SECTOR];
    if (!iso_read(16, 1, pvd)) {
        root_lsn = rd32(pvd + 156 + 2);
        root_size = rd32(pvd + 156 + 10);
        root_ok = 1;
        iso_walk(root_lsn, root_size, 0);
    }
    return 0;
}

/* ---------------------------------------------------------------- extracted folder */

static int name_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static uint32_t next_lsn;

/* directories get one virtual sector each in pre-order; files are collected and laid out after all directories */
static Dir *folder_scan(const char *host, uint32_t parent_lsn, int depth)
{
    Dir *d = new_dir(next_lsn++, DISC_SECTOR);
    int di = (int)(d - dirs);
    add_rec(d, "", 1, d->lsn, d->size);
    add_rec(d, "\1", 1, depth ? parent_lsn : d->lsn, DISC_SECTOR);
    DIR *h = opendir(host);
    if (!h) return d;
    char **names = 0;
    int nn = 0, capn = 0;
    while (struct dirent *e = readdir(h)) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        names = (char **)grow(names, &capn, nn + 1, sizeof *names);
        names[nn++] = dupstr(e->d_name, strlen(e->d_name));
    }
    closedir(h);
    for (int i = 0; i < nn; i++)
        for (char *p = names[i]; *p; p++) *p = (char)toupper((unsigned char)*p);
    qsort(names, (size_t)nn, sizeof *names, name_cmp);
    for (int i = 0; i < nn; i++) {
        size_t hl = strlen(host) + 1 + strlen(names[i]) + 1;
        char *full = (char *)malloc(hl);
        if (!full) abort();
        snprintf(full, hl, "%s/%s", host, names[i]);
        struct stat st;
        if (stat(full, &st)) { free(full); continue; }
        if (S_ISDIR(st.st_mode) && depth < 16) {
            uint32_t self = dirs[di].lsn;
            Dir *sub = folder_scan(full, self, depth + 1);
            uint32_t sl = sub->lsn, ss = sub->size;
            add_rec(&dirs[di], names[i], strlen(names[i]), sl, ss);   /* dirs may have moved (realloc) */
            free(full);
        } else if (S_ISREG(st.st_mode)) {
            files = (FFile *)grow(files, &capfiles, nfiles + 1, sizeof *files);
            FFile *f = &files[nfiles];
            f->path = full;
            f->size = (uint32_t)st.st_size;
            f->nsec = (f->size + DISC_SECTOR - 1) / DISC_SECTOR;
            f->lsn = (uint32_t)nfiles;   /* file index until the layout pass */
            nfiles++;
            char rn[300];
            snprintf(rn, sizeof rn, "%s;1", names[i]);
            add_rec(&dirs[di], rn, strlen(rn), (uint32_t)(nfiles - 1), f->size);
        } else free(full);
    }
    for (int i = 0; i < nn; i++) free(names[i]);
    free(names);
    return &dirs[di];
}

static int folder_open(const char *path)
{
    kind = DISC_FOLDER;
    next_lsn = 22;                        /* where an ISO's root usually sits; any value works */
    folder_scan(path, 0, 0);
    root_lsn = dirs[0].lsn;
    root_size = dirs[0].size;
    root_ok = 1;
    /* layout pass: files contiguous in directory order after the directory sectors */
    uint32_t lsn = next_lsn;
    for (int i = 0; i < nfiles; i++) { files[i].lsn = lsn; lsn += files[i].nsec ? files[i].nsec : 1; }
    folder_end = lsn;
    for (int i = 0; i < ndirs; i++)
        for (int k = 2; k < dirs[i].nrec; k++) {
            Rec *r = &dirs[i].rec[k];
            size_t l = strlen(r->name);
            if (l > 2 && !strcmp(r->name + l - 2, ";1")) r->lsn = files[r->lsn].lsn;
        }
    plat_log("disc: folder %s: %d dirs, %d files, %u sectors", path, ndirs, nfiles, folder_end);
    return 0;
}

static int folder_read(uint32_t lsn, uint32_t n, void *dst)
{
    uint8_t *o = (uint8_t *)dst;
    for (uint32_t s = 0; s < n; s++, o += DISC_SECTOR) {
        uint32_t l = lsn + s;
        if (l >= folder_end) return -1;
        memset(o, 0, DISC_SECTOR);
        int lo = 0, hi = nfiles - 1, fi = -1;
        while (lo <= hi) {
            int m = (lo + hi) / 2;
            if (files[m].lsn > l) hi = m - 1;
            else if (l >= files[m].lsn + files[m].nsec) lo = m + 1;
            else { fi = m; break; }
        }
        if (fi < 0) continue;   /* directory sector / padding: zeros */
        if (fi != ff_cur_i) {
            if (ff_cur) fclose(ff_cur);
            ff_cur = fopen(files[fi].path, "rb");
            ff_cur_i = ff_cur ? fi : -1;
            if (!ff_cur) return -1;
        }
        uint64_t off = (uint64_t)(l - files[fi].lsn) * DISC_SECTOR;
        uint32_t k = files[fi].size - (uint32_t)off < DISC_SECTOR ? files[fi].size - (uint32_t)off : DISC_SECTOR;
        disc_fseek(ff_cur, (long long)off, SEEK_SET);
        if (fread(o, 1, k, ff_cur) != k) return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------- public */

int disc_open(const char *path)
{
    disc_close();
    struct stat st;
    if (path && !stat(path, &st) && S_ISDIR(st.st_mode)) return folder_open(path);
    return path ? iso_open(path) : -1;
}

int disc_read(uint32_t lsn, uint32_t n, void *dst)
{
    if (kind == DISC_ISO) return iso_read(lsn, n, dst);
    if (kind == DISC_FOLDER) return folder_read(lsn, n, dst);
    return -1;
}

/* Same matching as the old per-call ISO9660 walk (runtime/cdvd.c before S1b): components split at \ or /, upper-
 * cased, at most 63 characters each (a longer one continues as the next component), compared with the record name
 * as is or with its ";..." suffix cut when the request has none; first match in record order wins. */
int disc_find(const char *path, uint32_t *lsn, uint32_t *size)
{
    if (!root_ok) return 0;
    uint32_t dlsn = root_lsn, dsize = root_size;
    const char *p = path;
    while (*p == '\\' || *p == '/') p++;
    for (;;) {
        char comp[64];
        int n = 0;
        while (*p && *p != '\\' && *p != '/' && n < 63) comp[n++] = (char)toupper((unsigned char)*p++);
        comp[n] = 0;
        int last = !*p;
        while (*p == '\\' || *p == '/') p++;
        Dir *d = get_dir(dlsn, dsize);
        int found = 0;
        int has_semi = strchr(comp, ';') != 0;
        for (int i = 0; d && i < d->nrec; i++) {
            const char *nm = d->rec[i].name;
            int ok = !strcmp(nm, comp);
            if (!ok && !has_semi) {
                const char *semi = strchr(nm, ';');
                ok = semi && (size_t)(semi - nm) == strlen(comp) && !strncmp(nm, comp, (size_t)(semi - nm));
            }
            if (ok) {
                dlsn = d->rec[i].lsn;
                dsize = d->rec[i].size;
                found = 1;
                break;
            }
        }
        if (!found) return 0;
        if (last) { *lsn = dlsn; *size = dsize; return 1; }
    }
}
