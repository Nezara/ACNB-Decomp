/* lib/sdk/libcdvd.cpp - libcdvd (sceCd*) and libkernl fileio (sceOpen / sceRead / sceLseek / sceClose / sceWrite /
 * sceGetstat / ... and the HDD-side calls) for the source port: thin shims over port/platform ps2_file (frozen HLE
 * semantics: sceCdRead completes inside the call, sceCdSync = 0, GetError 0; the HDD / pfs / dev9 calls report "no
 * device" -19 like runtime/fileio.c, so the game skips its HDD install path). The hybrid calls the original SDK code through runtime/bridge_sdk.c thunks instead (HLE hooks in
 * runtime/cdvd.c / fileio.c), so its ACNB_DISC_TRACE lines stay those of the original; nothing here is built there. */
#include "acnb/port.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
#include <stdint.h>
#include <string.h>
#include "../../platform/ps2_file.h"

/* trace address of a host buffer (the hybrid prints the guest address) */
static uint32_t sdk_trace_addr(const void *p) { return (uint32_t)(uintptr_t)p; }

int sceCdRead(unsigned int lsn, unsigned int sectors, void *buf, void *mode)
{
    (void)mode;
    return ps2f_cd_read(lsn, sectors, buf, sdk_trace_addr(buf));
}
int sceCdSync(int mode) { return ps2f_cd_sync((uint32_t)mode); }
int sceCdInit(int mode) { return ps2f_cd_init((uint32_t)mode); }
int sceCdMmode(int media) { return ps2f_cd_mmode((uint32_t)media); }
int sceCdSearchFile(void *fp, const char *name) { return ps2f_cd_search_file(name, (uint8_t *)fp); }
int sceCdGetError(void) { return ps2f_cd_get_error(); }

int sceOpen(const char *name, int flags) { return ps2f_open(name, (uint32_t)flags); }
int sceRead(int fd, void *buf, int n) { return ps2f_read((uint32_t)fd, buf, (uint32_t)n, sdk_trace_addr(buf)); }
int sceLseek(int fd, int offset, int whence) { return ps2f_lseek((uint32_t)fd, offset, whence); }
int sceClose(int fd) { return ps2f_close((uint32_t)fd); }
int sceWrite(int fd, const void *buf, int n) { return ps2f_write((uint32_t)fd, buf, (uint32_t)n, sdk_trace_addr(buf)); }
long long sceLseek64(int fd, long long offset, int whence) { return ps2f_lseek64((uint32_t)fd, offset, whence); }
int sceGetstat(const char *name, void *buf) { return ps2f_getstat(name, (uint8_t *)buf); }

/* fileio calls the hybrid HLE answers with a constant (runtime/fileio.c) */
#define SDK_NODEV (-19)
int sceFsReset(void) { return 0; }
int sceSync(const char *, int) { return 0; }
int sceChdir(const char *) { return 0; }
int sceIoctl2(int, int, void *, unsigned int, void *, unsigned int) { return SDK_NODEV; }
/* dev9x: DDIOC_OFF (0x4402) = 0: Iop_Init / fseth power-off spin until the DEV9 power-off succeeds, and every Iop_Init
 * after the first (DEV9 always loads, so s_isLoadDev9 = 1) would loop forever on -19 (mvsr i.LINK row -> Iop_Init(0x401),
 * mmanage -> Iop_Init(0x201)). No DEV9 hardware here: powering it off is a no-op that succeeds. Everything else -19. */
int sceDevctl(const char *name, int cmd, void *, unsigned int, void *, unsigned int)
{
    if (cmd == 0x4402 && name && !strcmp(name, "dev9x:")) return 0;
    return SDK_NODEV;
}
int sceChstat(const char *, void *, unsigned int) { return SDK_NODEV; }
int sceRename(const char *, const char *) { return SDK_NODEV; }
int sceRemove(const char *) { return SDK_NODEV; }
int sceReadlink(const char *, char *, unsigned int) { return SDK_NODEV; }
int sceSymlink(const char *, const char *) { return SDK_NODEV; }
int sceMkdir(const char *, int) { return SDK_NODEV; }
int sceRmdir(const char *) { return SDK_NODEV; }
int sceDopen(const char *) { return SDK_NODEV; }
int sceDclose(int) { return SDK_NODEV; }
int sceDread(int, void *) { return SDK_NODEV; }
int sceFormat(const char *, const char *, void *, int) { return SDK_NODEV; }
int sceMount(const char *, const char *, int, void *, int) { return SDK_NODEV; }
int sceUmount(const char *) { return SDK_NODEV; }
#endif
