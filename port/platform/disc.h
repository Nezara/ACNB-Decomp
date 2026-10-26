/* Host disc backend (phase 3 2.4): the game addresses data by LSN (2048-byte sectors), so the interface is the
 * sector level: find a path -> {lsn, size}, read sectors by LSN. Two sources:
 *   ISO image   = the reference (every check runs on it); ISO9660 tree parsed once at open and cached.
 *   folder      = extracted disc files (convenience): virtual LSNs, directories first, then each file contiguous in
 *                 directory order. LSNs also sit in game RAM (sceCdlFILE), so whole-RAM hashes differ from ISO mode by
 *                 design: never a check source.
 * One open disc per process. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define DISC_SECTOR 2048

enum { DISC_NONE, DISC_ISO, DISC_FOLDER };

int disc_open(const char *path);   /* ISO file or directory; 0 = ok, -1 = cannot open (previous disc closed) */
void disc_close(void);
int disc_kind(void);               /* DISC_NONE / DISC_ISO / DISC_FOLDER */
/* "\\DIR\\FILE.EXT;1" (separators \ or /, case-insensitive request, ";1" optional): 1 = found, 0 = not found */
int disc_find(const char *path, uint32_t *lsn, uint32_t *size);
int disc_read(uint32_t lsn, uint32_t n, void *dst);   /* 0 = ok, -1 = error (no disc, past the end) */

#ifdef __cplusplus
}
#endif
