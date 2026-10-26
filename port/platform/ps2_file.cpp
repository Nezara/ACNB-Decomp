/* PS2 libcdvd / fileio / IOP ioman + cdvdman semantics over the disc module (see ps2_file.h). Moved from
 * runtime/cdvd.c, fileio.c and iop_kern.c in phase 3 S1b without behaviour change (trace-diffed). */
#include "ps2_file.h"
#include "disc.h"
#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define f_seek _fseeki64
#define f_tell _ftelli64
#else
#define f_seek fseeko
#define f_tell ftello
#endif

#define E_NOENT (-2)
#define E_IO    (-5)
#define E_BADF  (-9)
#define E_INVAL (-22)
#define E_MFILE (-24)
#define E_ROFS  (-30)
#define E_NODEV (-19)

static char host_dir[600];

void ps2f_init(const char *dir) { snprintf(host_dir, sizeof host_dir, "%s", dir ? dir : "."); }

/* bytes [pos, pos + n) of the file at `lsn`; whole sectors straight into dst, partial ones through a bounce sector */
static int file_pread(uint32_t lsn, uint64_t pos, uint32_t n, uint8_t *dst)
{
    uint32_t done = 0;
    uint8_t sec[DISC_SECTOR];
    while (done < n) {
        uint64_t p = pos + done;
        uint32_t l = lsn + (uint32_t)(p / DISC_SECTOR), o = (uint32_t)(p % DISC_SECTOR);
        if (o == 0 && n - done >= DISC_SECTOR) {
            uint32_t cnt = (n - done) / DISC_SECTOR;
            if (disc_read(l, cnt, dst + done)) return E_IO;
            done += cnt * DISC_SECTOR;
        } else {
            if (disc_read(l, 1, sec)) return E_IO;
            uint32_t k = DISC_SECTOR - o;
            if (k > n - done) k = n - done;
            memcpy(dst + done, sec + o, k);
            done += k;
        }
    }
    return (int)n;
}

/* ---------------------------------------------------------------- EE libcdvd */

int ps2f_cd_init(uint32_t mode) { if (disc_trace_on) disc_trace("EE", "sceCdInit %u -> 1", mode); return 1; }
int ps2f_cd_sync(uint32_t mode) { if (disc_trace_on) disc_trace("EE", "sceCdSync %u -> 0", mode); return 0; }
int ps2f_cd_syncs(uint32_t mode) { if (disc_trace_on) disc_trace("EE", "sceCdSyncS %u -> 0", mode); return 0; }
int ps2f_cd_disk_ready(uint32_t mode) { if (disc_trace_on) disc_trace("EE", "sceCdDiskReady %u -> 2", mode); return 2; }
int ps2f_cd_get_error(void) { if (disc_trace_on) disc_trace("EE", "sceCdGetError -> 0"); return 0; }
int ps2f_cd_mmode(uint32_t) { return 1; }

int ps2f_cd_tray_req(uint32_t, void *traycnt)
{
    if (traycnt) memset(traycnt, 0, 4);
    return 1;
}

int ps2f_cd_read(uint32_t lsn, uint32_t n, void *dst, uint32_t ta)
{
    int ok = !disc_read(lsn, n, dst);
    if (disc_trace_on) {
        if (ok) disc_trace("EE", "sceCdRead lsn %u n %u buf %08x -> 1 crc %08x", lsn, n, ta, disc_crc32(dst, n * DISC_SECTOR));
        else disc_trace("EE", "sceCdRead lsn %u n %u buf %08x -> 0", lsn, n, ta);
    }
    return ok;
}

int ps2f_cd_search_file(const char *name, uint8_t *f)
{
    uint32_t lsn, size;
    if (!name || !disc_find(name, &lsn, &size)) {
        plat_log("cdvd: search %s -> not found", name ? name : "?");
        if (disc_trace_on) disc_trace("EE", "sceCdSearchFile %s -> 0", name ? name : "?");
        return 0;
    }
    memset(f, 0, 32);
    memcpy(f, &lsn, 4);
    memcpy(f + 4, &size, 4);
    const char *base = strrchr(name, '\\');
    snprintf((char *)f + 8, 16, "%s", base ? base + 1 : name);
    plat_log("cdvd: search %s -> lsn %u size %u", name, lsn, size);
    if (disc_trace_on) disc_trace("EE", "sceCdSearchFile %s -> 1 lsn %u size %u", name, lsn, size);
    return 1;
}

/* ---------------------------------------------------------------- EE fileio */

#define MAX_FD 32
struct Fd { int used, kind; uint32_t lsn, size; uint64_t pos; FILE *f; };   /* kind 1 = disc, 2 = host */
static Fd fds[MAX_FD];

int ps2f_open(const char *name, uint32_t flags)
{
    int fd;
    for (fd = 3; fd < MAX_FD && fds[fd].used; fd++) {}
    if (!name || fd >= MAX_FD) { if (disc_trace_on) disc_trace("EE", "sceOpen %s %x -> -24", name ? name : "?", flags); return E_MFILE; }
    Fd *d = &fds[fd];
    memset(d, 0, sizeof *d);
    if (!strncmp(name, "cdrom0:", 7) || !strncmp(name, "cdrom:", 6)) {
        const char *p = strchr(name, ':') + 1;
        if (!disc_find(p, &d->lsn, &d->size)) {
            plat_log("fileio: open %s -> ENOENT", name);
            if (disc_trace_on) disc_trace("EE", "sceOpen %s %x -> %d", name, flags, E_NOENT);
            return E_NOENT;
        }
        d->used = 1; d->kind = 1;
        plat_log("fileio: open %s -> fd %d (lsn %u size %u)", name, fd, d->lsn, d->size);
        if (disc_trace_on) disc_trace("EE", "sceOpen %s %x -> %d lsn %u size %u", name, flags, fd, d->lsn, d->size);
        return fd;
    }
    if (!strncmp(name, "host0:", 6) || !strncmp(name, "host:", 5)) {
        char path[700];
        snprintf(path, sizeof path, "%s\\%s", host_dir, strchr(name, ':') + 1);
        const char *mode = (flags & 3) == 1 ? "rb" : (flags & 0x200) ? ((flags & 0x100) ? "ab+" : "wb+") : "rb+";
        d->f = fopen(path, mode);
        if (!d->f) {
            plat_log("fileio: open %s -> ENOENT", name);
            if (disc_trace_on) disc_trace("EE", "sceOpen %s %x -> %d", name, flags, E_NOENT);
            return E_NOENT;
        }
        d->used = 1; d->kind = 2;
        if (disc_trace_on) disc_trace("EE", "sceOpen %s %x -> %d", name, flags, fd);
        return fd;
    }
    plat_log("fileio: open %s (flags %x) -> ENODEV", name, flags);
    if (disc_trace_on) disc_trace("EE", "sceOpen %s %x -> %d", name, flags, E_NODEV);
    return E_NODEV;
}

static Fd *getfd(uint32_t fd) { return fd < MAX_FD && fds[fd].used ? &fds[fd] : 0; }

int ps2f_fd_valid(uint32_t fd) { return getfd(fd) != 0; }

int ps2f_close(uint32_t fd)
{
    Fd *d = getfd(fd);
    if (disc_trace_on) disc_trace("EE", "sceClose %d -> %d", (int)fd, d ? 0 : E_BADF);
    if (!d) return E_BADF;
    if (d->f) fclose(d->f);
    d->used = 0;
    return 0;
}

int ps2f_read(uint32_t fd, void *dst, uint32_t n, uint32_t ta)
{
    Fd *d = getfd(fd);
    if (!d) { if (disc_trace_on) disc_trace("EE", "sceRead %d %08x %u -> %d", (int)fd, ta, n, E_BADF); return E_BADF; }
    int32_t r;
    if (d->kind == 1) {
        if (d->pos >= d->size) r = 0;
        else {
            uint32_t k = d->pos + n > d->size ? (uint32_t)(d->size - d->pos) : n;
            r = file_pread(d->lsn, d->pos, k, (uint8_t *)dst);
            if (r >= 0) d->pos += k;
        }
    } else r = (int32_t)fread(dst, 1, n, d->f);
    if (disc_trace_on) disc_trace("EE", "sceRead %d %08x %u -> %d crc %08x", (int)fd, ta, n, r, r > 0 ? disc_crc32(dst, (uint32_t)r) : 0);
    return r;
}

int ps2f_write(uint32_t fd, const void *src, uint32_t n, uint32_t ta)
{
    if (fd == 1 || fd == 2) { plat_log("[tty] %.*s", (int)n, (const char *)src); return (int)n; }
    Fd *d = getfd(fd);
    int32_t r = !d ? E_BADF : d->kind != 2 ? E_ROFS : (int32_t)fwrite(src, 1, n, d->f);
    if (disc_trace_on) disc_trace("EE", "sceWrite %d %08x %u -> %d crc %08x", (int)fd, ta, n, r, r > 0 ? disc_crc32(src, (uint32_t)r) : 0);
    return r;
}

static int64_t do_seek(Fd *d, int64_t off, int whence)
{
    if (d->kind == 2) {
        if (f_seek(d->f, off, whence)) return E_INVAL;
        return f_tell(d->f);
    }
    int64_t base = whence == 0 ? 0 : whence == 1 ? (int64_t)d->pos : (int64_t)d->size;
    if (base + off < 0) return E_INVAL;
    d->pos = (uint64_t)(base + off);
    return (int64_t)d->pos;
}

int32_t ps2f_lseek(uint32_t fd, int32_t off, int32_t whence)
{
    Fd *d = getfd(fd);
    int32_t r = d ? (int32_t)do_seek(d, off, whence) : E_BADF;
    if (disc_trace_on) disc_trace("EE", "sceLseek %d %d %d -> %d", (int)fd, (int)off, (int)whence, r);
    return r;
}

int64_t ps2f_lseek64(uint32_t fd, int64_t off, int32_t whence)
{
    Fd *d = getfd(fd);
    int64_t r = d ? do_seek(d, off, whence) : E_BADF;
    if (disc_trace_on) disc_trace("EE", "sceLseek64 %d %lld %d -> %lld", (int)fd, (long long)off, (int)whence, (long long)r);
    return r;
}

int ps2f_getstat(const char *name, uint8_t *st)
{
    uint32_t lsn, size;
    if (name && !strncmp(name, "cdrom0:", 7) && disc_find(name + 7, &lsn, &size)) {
        if (st) { memset(st, 0, 64); uint32_t m = 0x2124; memcpy(st, &m, 4); memcpy(st + 8, &size, 4); }
        if (disc_trace_on) disc_trace("EE", "sceGetstat %s -> 0 size %u", name, size);
        return 0;
    }
    if (disc_trace_on) disc_trace("EE", "sceGetstat %s -> %d", name ? name : "?", E_NOENT);
    return E_NOENT;
}

/* ---------------------------------------------------------------- IOP ioman / cdvdman */

#define MAX_IFD 16
struct IFd { int used; uint32_t lsn, size, pos; };
static IFd ifd[MAX_IFD];

int ps2f_iop_open(const char *name, uint32_t flags)
{
    const char *p = strchr(name, ':');
    if (!p || strncmp(name, "cdrom", 5)) {
        plat_log("iop: open %s -> ENODEV", name);
        if (disc_trace_on) disc_trace("IOP", "open %s %x -> -19", name, flags);
        return E_NODEV;
    }
    for (int i = 1; i < MAX_IFD; i++) {
        if (ifd[i].used) continue;
        if (!disc_find(p + 1, &ifd[i].lsn, &ifd[i].size)) {
            plat_log("iop: open %s -> ENOENT", name);
            if (disc_trace_on) disc_trace("IOP", "open %s %x -> -2", name, flags);
            return E_NOENT;
        }
        ifd[i].used = 1;
        ifd[i].pos = 0;
        if (disc_trace_on) disc_trace("IOP", "open %s %x -> %d lsn %u size %u", name, flags, i, ifd[i].lsn, ifd[i].size);
        return i;
    }
    if (disc_trace_on) disc_trace("IOP", "open %s %x -> -24", name, flags);
    return E_MFILE;
}

static IFd *ifdp(uint32_t fd) { return fd < MAX_IFD && ifd[fd].used ? &ifd[fd] : 0; }

int ps2f_iop_fd_info(uint32_t fd, uint32_t *lsn, uint32_t *size)
{
    IFd *f = ifdp(fd);
    if (!f) return 0;
    *lsn = f->lsn;
    *size = f->size;
    return 1;
}

int ps2f_iop_close(uint32_t fd)
{
    IFd *f = ifdp(fd);
    if (disc_trace_on) disc_trace("IOP", "close %d -> %d", (int)fd, f ? 0 : E_BADF);
    if (f) f->used = 0;
    return f ? 0 : E_BADF;
}

int ps2f_iop_read(uint32_t fd, void *dst, uint32_t n0, uint32_t ta)
{
    IFd *f = ifdp(fd);
    if (!f || !dst) { if (disc_trace_on) disc_trace("IOP", "read %d %08x %u -> -9", (int)fd, ta, n0); return E_BADF; }
    uint32_t n = n0;
    if (f->pos >= f->size) { if (disc_trace_on) disc_trace("IOP", "read %d %08x %u -> 0", (int)fd, ta, n0); return 0; }
    if (f->pos + n > f->size) n = f->size - f->pos;
    int r = file_pread(f->lsn, f->pos, n, (uint8_t *)dst);
    if (disc_trace_on) disc_trace("IOP", "read %d %08x %u -> %d crc %08x", (int)fd, ta, n0, r, r > 0 ? disc_crc32(dst, (uint32_t)r) : 0);
    if (r > 0) f->pos += r;
    return r;
}

int32_t ps2f_iop_lseek(uint32_t fd, int32_t off, uint32_t whence)
{
    IFd *f = ifdp(fd);
    if (!f) { if (disc_trace_on) disc_trace("IOP", "lseek %d %d %u -> -9", (int)fd, (int)off, whence); return E_BADF; }
    uint32_t np = whence == 0 ? (uint32_t)off : whence == 1 ? f->pos + off : f->size + off;
    f->pos = np;
    if (disc_trace_on) disc_trace("IOP", "lseek %d %d %u -> %d", (int)fd, off, whence, (int)np);
    return (int32_t)np;
}

int ps2f_iop_cd_read(uint32_t lsn, uint32_t n, void *dst, uint32_t ta, int has_cb)
{
    if (!dst || disc_read(lsn, n, dst)) {
        plat_log("iop: sceCdRead lsn %u n %u failed", lsn, n);
        if (disc_trace_on) disc_trace("IOP", "sceCdRead lsn %u n %u buf %08x -> 0", lsn, n, ta);
        return 0;
    }
    if (disc_trace_on) disc_trace("IOP", "sceCdRead lsn %u n %u buf %08x -> 1 crc %08x cb %d", lsn, n, ta, disc_crc32(dst, n * DISC_SECTOR), has_cb != 0);
    return 1;
}

int ps2f_iop_cd_sync(uint32_t mode) { if (disc_trace_on) disc_trace("IOP", "sceCdSync %u -> 0", mode); return 0; }

int ps2f_iop_cd_search_file(const char *name, uint32_t *lsn, uint32_t *size)
{
    if (!disc_find(name, lsn, size)) {
        plat_log("iop: sceCdSearchFile %s not found", name);
        if (disc_trace_on) disc_trace("IOP", "sceCdSearchFile %s -> 0", name);
        return 0;
    }
    if (disc_trace_on) disc_trace("IOP", "sceCdSearchFile %s -> 1 lsn %u size %u", name, *lsn, *size);
    return 1;
}
