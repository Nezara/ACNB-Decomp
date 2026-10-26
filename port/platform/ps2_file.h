/* PS2 libcdvd + fileio semantics on host pointers (phase 3 2.4), over the disc module. Frozen to the hybrid's HLE as
 * of s90, byte for byte and call for call: sceCdRead completes synchronously inside the call, sceCdSync(any) = 0
 * (idle), GetError 0, DiskReady 2, Mmode 1, SearchFile fills 32 bytes; error codes ENOENT -2 / EBADF -9 / ENODEV -19
 * / EMFILE -24. Any "realistic latency" would move load-completion frames (= sim state): not offered.
 * Every traced call writes its ACNB_DISC_TRACE line here; `ta` arguments are the caller's game-visible buffer
 * address, printed in the trace instead of the host pointer (the hybrid passes the guest address). */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* host0: root ("<root>\\port\\host" in the hybrid); paths are joined as "<dir>\\<rest>" */
void ps2f_init(const char *host_dir);

/* ---- EE libcdvd */
int ps2f_cd_init(uint32_t mode);                 /* 1 */
int ps2f_cd_sync(uint32_t mode);                 /* 0 */
int ps2f_cd_syncs(uint32_t mode);                /* 0 */
int ps2f_cd_disk_ready(uint32_t mode);           /* 2 */
int ps2f_cd_get_error(void);                     /* 0 */
int ps2f_cd_mmode(uint32_t mode);                /* 1 (untraced) */
int ps2f_cd_tray_req(uint32_t mode, void *traycnt);   /* writes u32 0 if traycnt; 1 (untraced) */
int ps2f_cd_read(uint32_t lsn, uint32_t nsec, void *dst, uint32_t ta);   /* 1 = ok, 0 = read error */
/* sceCdlFILE (32 bytes): u32 lsn, u32 size, char name[16] (base name), 8 zero bytes; 1 = found, 0 = not found
 * (name may be 0 = not found, traced as "?"; out untouched when not found) */
int ps2f_cd_search_file(const char *name, uint8_t *out32);

/* ---- EE fileio (sceOpen ...): fds 3..31; cdrom0: / cdrom: = disc, host0: / host: = host_dir, others ENODEV */
int ps2f_open(const char *name, uint32_t flags);
int ps2f_fd_valid(uint32_t fd);
int ps2f_close(uint32_t fd);
int ps2f_read(uint32_t fd, void *dst, uint32_t n, uint32_t ta);           /* bytes / 0 at EOF / -9 / -5 */
int ps2f_write(uint32_t fd, const void *src, uint32_t n, uint32_t ta);    /* fd 1 / 2 = tty log; disc fd -30 */
int32_t ps2f_lseek(uint32_t fd, int32_t off, int32_t whence);
int64_t ps2f_lseek64(uint32_t fd, int64_t off, int32_t whence);
int ps2f_getstat(const char *name, uint8_t *st64);   /* cdrom0: only: mode 0x2124, size at +8; else -2 */

/* ---- IOP ioman (cdrom only, fds 1..15) / cdvdman, for the interpreted IRX modules */
int ps2f_iop_open(const char *name, uint32_t flags);
int ps2f_iop_fd_info(uint32_t fd, uint32_t *lsn, uint32_t *size);   /* 1 = open fd */
int ps2f_iop_close(uint32_t fd);
int ps2f_iop_read(uint32_t fd, void *dst, uint32_t n, uint32_t ta);   /* dst 0 = bad buffer (-9) */
int32_t ps2f_iop_lseek(uint32_t fd, int32_t off, uint32_t whence);
/* synchronous; the caller runs the sceCdCallback after a 1 (has_cb only goes into the trace) */
int ps2f_iop_cd_read(uint32_t lsn, uint32_t nsec, void *dst, uint32_t ta, int has_cb);
int ps2f_iop_cd_sync(uint32_t mode);             /* 0 */
/* 1 = found (lsn / size set; the caller writes its sceCdlFILE), 0 = not found */
int ps2f_iop_cd_search_file(const char *name, uint32_t *lsn, uint32_t *size);

#ifdef __cplusplus
}
#endif
