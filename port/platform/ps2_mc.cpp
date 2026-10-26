/* PS2 libmc semantics over a host directory (see ps2_mc.h). Moved from runtime/mc.c (s96 B5); only the timestamps
 * changed (mtime in both date fields). */
#include "ps2_mc.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define MC_SEP '\\'
static int mc_mkdir(const char *p) { return _mkdir(p); }
static int mc_rmdir(const char *p) { return _rmdir(p); }
#else
#include <dirent.h>
#include <unistd.h>
#define MC_SEP '/'
static int mc_mkdir(const char *p) { return mkdir(p, 0777); }
static int mc_rmdir(const char *p) { return rmdir(p); }
#endif

static int pend_cmd, pend_res, pend;
static char cwd[2][256] = {"/", "/"};
static int card_seen;
static FILE *mfd[32];
static char root[600];

#define TRACE(...) do { if (mc_trace_on && mc_trace_enabled()) mc_trace("MC", __VA_ARGS__); } while (0)

static void done(int cmd, int res) { pend_cmd = cmd; pend_res = res; pend = 1; }
static void put32(int *p, int v) { if (p) memcpy(p, &v, 4); }

extern "C" void ps2mc_init(const char *root_dir)
{
    const char *alt = getenv("ACNB_MEMCARD");   /* tests: use another card directory (created if missing) */
    if (alt && *alt) {
        snprintf(root, sizeof root, "%s", alt);
        mc_mkdir(root);
        plat_log("mc: card directory %s", root);
    } else {
        char p[700];
        snprintf(p, sizeof p, "%s%cport%cmemcard", root_dir, MC_SEP, MC_SEP);
        snprintf(root, sizeof root, "%s%cmc0", p, MC_SEP);
        mc_mkdir(p);
        mc_mkdir(root);
    }
}

extern "C" const char *ps2mc_card_dir(void) { return root; }

/* Resolve a card path relative to the current directory into a host path. */
static void host_path(uint32_t port, const char *name, char *out, size_t n)
{
    char p[512];
    if (name[0] == '/') snprintf(p, sizeof p, "%s", name);
    else snprintf(p, sizeof p, "%s%s%s", cwd[port], cwd[port][strlen(cwd[port]) - 1] == '/' ? "" : "/", name);
    snprintf(out, n, "%s%s", root, p);
    for (char *q = out; *q; q++) if (*q == '/') *q = MC_SEP;
}

static int port_ok(uint32_t port) { return port == 0; }

extern "C" int ps2mc_mc_init(void)
{
    TRACE("Init -> 0");
    return 0;
}

extern "C" int ps2mc_get_info(uint32_t port, uint32_t slot, int *type, int *free_, int *format)
{
    if (!port_ok(port)) {
        put32(type, 0);
        put32(free_, 0);
        put32(format, 0);
        done(PS2MC_GETINFO, -10);
    } else {
        put32(type, 2);       /* PS2 card */
        put32(free_, 7000);   /* free clusters (KB) */
        put32(format, 1);
        done(PS2MC_GETINFO, card_seen++ ? 0 : -1);
    }
    TRACE("GetInfo %u %u -> 0 (%d)", port, slot, pend_res);
    return 0;
}

extern "C" int ps2mc_sync(uint32_t mode, int *cmd, int *result)
{
    if (!pend) { TRACE("Sync %u -> -1", mode); return -1; }
    put32(cmd, pend_cmd);
    put32(result, pend_res);
    pend = 0;
    TRACE("Sync %u -> 1 cmd %d res %d", mode, pend_cmd, pend_res);
    return 1;
}

extern "C" int ps2mc_open(uint32_t port, uint32_t slot, const char *name, uint32_t mode)
{
    char hp[700];
    if (!port_ok(port) || !name) { done(PS2MC_OPEN, -10); TRACE("Open %u %u %s %x -> 0 (-10)", port, slot, name ? name : "?", mode); return 0; }
    host_path(port, name, hp, sizeof hp);
    if (mode & 0x40) {                  /* mkdir */
        int r = mc_mkdir(hp);
        plat_log("mc: mkdir %s -> %d", name, r);
        done(PS2MC_OPEN, r == 0 ? 0 : -4);
        TRACE("Open %u %u %s %x -> 0 (%d)", port, slot, name, mode, pend_res);
        return 0;
    }
    int fd;
    for (fd = 0; fd < 32 && mfd[fd]; fd++) {}
    if (fd == 32) { done(PS2MC_OPEN, -7); TRACE("Open %u %u %s %x -> 0 (-7)", port, slot, name, mode); return 0; }
    struct stat st;
    int exists = stat(hp, &st) == 0;
    if (!exists && !(mode & 0x200)) {
        plat_log("mc: open %s -> not found", name);
        done(PS2MC_OPEN, -4);
        TRACE("Open %u %u %s %x -> 0 (-4)", port, slot, name, mode);
        return 0;
    }
    const char *m = exists ? ((mode & 3) == 1 ? "rb" : (mode & 0x400) ? "wb+" : "rb+") : "wb+";
    mfd[fd] = fopen(hp, m);
    plat_log("mc: open %s mode %x -> %d", name, mode, mfd[fd] ? fd : -4);
    done(PS2MC_OPEN, mfd[fd] ? fd : -4);
    TRACE("Open %u %u %s %x -> 0 (%d)", port, slot, name, mode, pend_res);
    return 0;
}

static FILE *getf(uint32_t fd) { return fd < 32 ? mfd[fd] : 0; }

extern "C" int ps2mc_close(uint32_t fd)
{
    FILE *f = getf(fd);
    if (f) { fclose(f); mfd[fd] = 0; }
    done(PS2MC_CLOSE, f ? 0 : -4);
    TRACE("Close %u -> 0 (%d)", fd, pend_res);
    return 0;
}

extern "C" int ps2mc_seek(uint32_t fd, int32_t offset, int32_t origin)
{
    FILE *f = getf(fd);
    if (!f) done(PS2MC_SEEK, -4);
    else {
        fseek(f, offset, origin);
        done(PS2MC_SEEK, (int)ftell(f));
    }
    TRACE("Seek %u %d %d -> 0 (%d)", fd, offset, origin, pend_res);
    return 0;
}

extern "C" int ps2mc_read(uint32_t fd, void *dst, uint32_t n)
{
    FILE *f = getf(fd);
    if (!f || !dst) done(PS2MC_READ, -4);
    else done(PS2MC_READ, (int)fread(dst, 1, n, f));
    TRACE("Read %u %u -> 0 (%d)", fd, n, pend_res);
    return 0;
}

extern "C" int ps2mc_write(uint32_t fd, const void *src, uint32_t n)
{
    FILE *f = getf(fd);
    if (!f || !src) done(PS2MC_WRITE, -4);
    else {
        done(PS2MC_WRITE, (int)fwrite(src, 1, n, f));
        fflush(f);
    }
    TRACE("Write %u %u -> 0 (%d)", fd, n, pend_res);
    return 0;
}

extern "C" int ps2mc_flush(uint32_t fd)
{
    FILE *f = getf(fd);
    if (f) fflush(f);
    done(PS2MC_FLUSH, f ? 0 : -4);
    TRACE("Flush %u -> 0 (%d)", fd, pend_res);
    return 0;
}

static int is_dir_path(const char *hp)
{
    struct stat st;
    return stat(hp, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

extern "C" int ps2mc_chdir(uint32_t port, uint32_t slot, const char *nd, char *olddir)
{
    if (!port_ok(port) || !nd) { done(PS2MC_CHDIR, -10); TRACE("Chdir %u %u %s -> 0 (-10)", port, slot, nd ? nd : "?"); return 0; }
    if (olddir) strcpy(olddir, cwd[port]);
    char np[256];
    if (nd[0] == '/') snprintf(np, sizeof np, "%s", nd);
    else if (!strcmp(nd, "..")) {
        snprintf(np, sizeof np, "%s", cwd[port]);
        char *s = strrchr(np, '/');
        if (s && s != np) *s = 0; else strcpy(np, "/");
    } else snprintf(np, sizeof np, "%s%s%s", cwd[port], strcmp(cwd[port], "/") ? "/" : "", nd);
    char hp[700];
    host_path(port, np, hp, sizeof hp);
    if (!is_dir_path(hp)) done(PS2MC_CHDIR, -4);
    else {
        snprintf(cwd[port], sizeof cwd[port], "%s", np);
        done(PS2MC_CHDIR, 0);
    }
    TRACE("Chdir %u %u %s -> 0 (%d)", port, slot, nd, pend_res);
    return 0;
}

static void put_date(uint8_t *d, time_t t)
{
    struct tm *tm = localtime(&t);
    d[0] = 0; d[1] = (uint8_t)tm->tm_sec; d[2] = (uint8_t)tm->tm_min; d[3] = (uint8_t)tm->tm_hour;
    d[4] = (uint8_t)tm->tm_mday; d[5] = (uint8_t)(tm->tm_mon + 1);
    uint16_t y = (uint16_t)(tm->tm_year + 1900);
    memcpy(d + 6, &y, 2);
}

/* sceMcTblGetDir (64 bytes): create[8] modify[8] size(u32) attr(u16) rsv(u16) rsv(u32) pda(u32) name[32]; mt goes
 * into both dates */
static void fill_ent(uint8_t *e, const char *name, int is_dir, uint32_t size, time_t mt)
{
    memset(e, 0, 64);
    put_date(e, mt);
    put_date(e + 8, mt);
    memcpy(e + 16, &size, 4);
    uint16_t attr = is_dir ? 0x8427 : 0x8497;
    memcpy(e + 20, &attr, 2);
    snprintf((char *)e + 32, 32, "%s", name);
}

static int wild(const char *p, const char *s)
{
    if (!*p) return !*s;
    if (*p == '*') return wild(p + 1, s) || (*s && wild(p, s + 1));
    if (*p == '?' ) return *s && wild(p + 1, s + 1);
    return *p == *s && wild(p + 1, s + 1);
}

extern "C" int ps2mc_get_dir(uint32_t port, uint32_t slot, const char *name, uint32_t mode, uint32_t maxent, uint8_t *table)
{
    if (!port_ok(port) || !name) {
        done(PS2MC_GETDIR, -10);
        TRACE("GetDir %u %u %s %u %u -> 0 (-10)", port, slot, name ? name : "?", mode, maxent);
        return 0;
    }
    char dir[512], pat[128];
    const char *slash = strrchr(name, '/');
    if (slash) { snprintf(dir, sizeof dir, "%.*s", (int)(slash - name), name); snprintf(pat, sizeof pat, "%s", slash + 1); }
    else { dir[0] = 0; snprintf(pat, sizeof pat, "%s", name); }
    if (!dir[0] && name[0] == '/') strcpy(dir, "/");
    char hp[700], q[760];
    host_path(port, dir[0] ? dir : ".", hp, sizeof hp);
    int is_root = (!strcmp(dir, "/") || (!dir[0] && !strcmp(cwd[port], "/")));
    int n = 0;
    struct stat st;
    if (!is_root && stat(hp, &st) == 0 && n < (int)maxent) {
        if (wild(pat, ".")) fill_ent(table + 64 * n++, ".", 1, 0, st.st_mtime);
        if (wild(pat, "..") && n < (int)maxent) fill_ent(table + 64 * n++, "..", 1, 0, st.st_mtime);
    }
#ifdef _WIN32
    snprintf(q, sizeof q, "%s\\*", hp);
    struct _finddata_t fd;
    intptr_t h = _findfirst(q, &fd);
    if (h != -1) {
        do {
            if (!strcmp(fd.name, ".") || !strcmp(fd.name, "..")) continue;
            if (!wild(pat, fd.name) || n >= (int)maxent) continue;
            fill_ent(table + 64 * n++, fd.name, (fd.attrib & _A_SUBDIR) != 0, (uint32_t)fd.size, fd.time_write);
        } while (_findnext(h, &fd) == 0);
        _findclose(h);
    }
#else
    DIR *dp = opendir(hp);
    if (dp) {
        struct dirent *de;
        while ((de = readdir(dp))) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
            if (!wild(pat, de->d_name) || n >= (int)maxent) continue;
            snprintf(q, sizeof q, "%s/%s", hp, de->d_name);
            struct stat es;
            if (stat(q, &es)) continue;
            int d = (es.st_mode & S_IFMT) == S_IFDIR;
            fill_ent(table + 64 * n++, de->d_name, d, d ? 0u : (uint32_t)es.st_size, es.st_mtime);
        }
        closedir(dp);
    }
#endif
    plat_log("mc: getdir %s -> %d", name, n);
    done(PS2MC_GETDIR, n);
    TRACE("GetDir %u %u %s %u %u -> 0 (%d)", port, slot, name, mode, maxent, n);
    return 0;
}

extern "C" int ps2mc_delete(uint32_t port, uint32_t slot, const char *name)
{
    char hp[700];
    if (!port_ok(port) || !name) done(PS2MC_DELETE, -10);
    else {
        host_path(port, name, hp, sizeof hp);
        int r = remove(hp) == 0 || mc_rmdir(hp) == 0;
        done(PS2MC_DELETE, r ? 0 : -4);
    }
    TRACE("Delete %u %u %s -> 0 (%d)", port, slot, name ? name : "?", pend_res);
    return 0;
}

extern "C" int ps2mc_set_file_info(uint32_t port, uint32_t slot, const char *name)
{
    done(PS2MC_SETINFO, 0);
    TRACE("SetFileInfo %u %u %s -> 0 (0)", port, slot, name ? name : "?");
    return 0;
}

extern "C" int ps2mc_rename(uint32_t port, uint32_t slot, const char *o, const char *nn)
{
    char a[700], b[700];
    if (!port_ok(port) || !o || !nn) done(PS2MC_SETINFO, -10);   /* (sic: mc.c latched the SetFileInfo code) */
    else {
        host_path(port, o, a, sizeof a);
        snprintf(b, sizeof b, "%s", a);
        char *s = strrchr(b, MC_SEP);
        snprintf(s + 1, sizeof b - (s + 1 - b), "%s", nn);
        done(PS2MC_SETINFO, rename(a, b) == 0 ? 0 : -4);
    }
    TRACE("Rename %u %u %s %s -> 0 (%d)", port, slot, o ? o : "?", nn ? nn : "?", pend_res);
    return 0;
}

extern "C" int ps2mc_format(uint32_t port, uint32_t slot)
{
    done(PS2MC_FORMAT, port_ok(port) ? 0 : -10);
    TRACE("Format %u %u -> 0 (%d)", port, slot, pend_res);
    return 0;
}

extern "C" int ps2mc_unformat(uint32_t port, uint32_t slot)
{
    done(PS2MC_UNFORMAT, port_ok(port) ? 0 : -10);
    TRACE("Unformat %u %u -> 0 (%d)", port, slot, pend_res);
    return 0;
}

extern "C" int ps2mc_get_ent_space(uint32_t port, uint32_t slot, const char *path)
{
    done(PS2MC_GETENT, 64);
    TRACE("GetEntSpace %u %u %s -> 0 (64)", port, slot, path ? path : "?");
    return 0;
}

extern "C" int ps2mc_ud_check_new_card(void)
{
    TRACE("UdCheckNewCard -> 0");
    return 0;
}
