/* PS2 libmc semantics (sceMc*) on host pointers (phase 3 boot slice B5), over a host directory. Moved out of the
 * hybrid's runtime/mc.c (frozen to it as of s96, call for call): memory card port 0 is the host directory (default
 * "<root>\port\memcard\mc0", ACNB_MEMCARD = another one, created if missing); port 1 is empty (-10). Every command
 * completes inside the call and latches its result for the next sceMcSync (one latch: a second command before a
 * Sync overwrites it); Sync without a pending result returns -1 and writes nothing.
 *   - GetInfo port 0: type 2 (PS2 card), free 7000, format 1; latched -1 on the very first GetInfo of the process
 *     ("new card", card_seen), 0 afterwards. Init does not reset that.
 *   - GetDir: name split at the last '/', '*' / '?' wildcards, "." / ".." first unless at the root, up to maxent 64-byte
 *     sceMcTblGetDir entries (attr 0x8427 dir / 0x8497 file); latched = entry count. Dates: host mtime (local time)
 *     in both the created and modified fields (s96 user decision: portable; mc.c used the Windows creation time).
 *   - Open: mode 0x40 = mkdir, 0x200 create, 0x400 truncate, (mode & 3) == 1 read only; fds 0..31.
 *   - Format / Unformat / SetFileInfo latch 0 without touching the card; GetEntSpace latches 64.
 * Every call writes its ACNB_MC_TRACE line (platform/plat.h); pointers are never printed. Generic: no ACNB addresses. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { PS2MC_GETINFO = 1, PS2MC_OPEN, PS2MC_CLOSE, PS2MC_SEEK, PS2MC_READ, PS2MC_WRITE, PS2MC_FLUSH = 10,
       PS2MC_CHDIR = 12, PS2MC_GETDIR, PS2MC_SETINFO, PS2MC_DELETE, PS2MC_FORMAT, PS2MC_UNFORMAT, PS2MC_GETENT };

/* root_dir: the game root ("<root>\port\memcard\mc0" unless ACNB_MEMCARD is set); creates the card directory */
void ps2mc_init(const char *root_dir);
const char *ps2mc_card_dir(void);   /* host directory of port 0 (quick_match.c reads saves from it) */

/* all return what the libmc call returns (0 = command issued; Sync 1 / -1); outputs may be 0 */
int ps2mc_mc_init(void);                                                      /* 0 */
int ps2mc_get_info(uint32_t port, uint32_t slot, int *type, int *free_, int *format);
int ps2mc_sync(uint32_t mode, int *cmd, int *result);
int ps2mc_get_dir(uint32_t port, uint32_t slot, const char *name, uint32_t mode, uint32_t maxent, uint8_t *table);
int ps2mc_open(uint32_t port, uint32_t slot, const char *name, uint32_t mode);
int ps2mc_close(uint32_t fd);
int ps2mc_seek(uint32_t fd, int32_t offset, int32_t origin);
int ps2mc_read(uint32_t fd, void *dst, uint32_t n);         /* dst 0 = bad buffer (-4) */
int ps2mc_write(uint32_t fd, const void *src, uint32_t n);  /* src 0 = bad buffer (-4) */
int ps2mc_flush(uint32_t fd);
int ps2mc_chdir(uint32_t port, uint32_t slot, const char *newdir, char *olddir);
int ps2mc_delete(uint32_t port, uint32_t slot, const char *name);
int ps2mc_set_file_info(uint32_t port, uint32_t slot, const char *name);
int ps2mc_rename(uint32_t port, uint32_t slot, const char *oldname, const char *newname);
int ps2mc_format(uint32_t port, uint32_t slot);
int ps2mc_unformat(uint32_t port, uint32_t slot);
int ps2mc_get_ent_space(uint32_t port, uint32_t slot, const char *path);
int ps2mc_ud_check_new_card(void);                                            /* 0 */

#ifdef __cplusplus
}
#endif
