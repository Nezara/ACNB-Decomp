/* lib/sdk/libmc.cpp - libmc (sceMc*) for the source port: thin shims over port/platform ps2_mc (the libmc stand-in
 * shared with the hybrid's runtime/mc.c: commands complete inside the call, sceMcSync hands out the latched result).
 * The hybrid calls the original SDK code through runtime/bridge_sdk.c thunks instead (HLE hooks in runtime/mc.c), so
 * both builds write the same ACNB_MC_TRACE lines; nothing here is built there. Boot subset (B5): the four calls of
 * fslibmc's title check; S1 (load path): Open / Close / Seek / Read; S2 (save path): Write / Mkdir / Format / Unformat / Chdir. */
#include "acnb/port.h"
#include "acnb/sdk.h"

#ifdef ACNB_SOURCEPORT
#include <stdint.h>
#include "../../platform/ps2_mc.h"

int sceMcInit(void) { return ps2mc_mc_init(); }
int sceMcGetInfo(int port, int slot, int *type, int *free, int *format)
{
    return ps2mc_get_info((uint32_t)port, (uint32_t)slot, type, free, format);
}
int sceMcSync(int mode, int *cmd, int *result) { return ps2mc_sync((uint32_t)mode, cmd, result); }
int sceMcGetDir(int port, int slot, const char *name, unsigned int mode, int maxent, void *table)
{
    return ps2mc_get_dir((uint32_t)port, (uint32_t)slot, name, mode, (uint32_t)maxent, (uint8_t *)table);
}
int sceMcOpen(int port, int slot, const char *name, int mode)
{
    return ps2mc_open((uint32_t)port, (uint32_t)slot, name, (uint32_t)mode);
}
int sceMcClose(int fd) { return ps2mc_close((uint32_t)fd); }
int sceMcSeek(int fd, int offset, int origin) { return ps2mc_seek((uint32_t)fd, offset, origin); }
int sceMcRead(int fd, void *buff, int size) { return ps2mc_read((uint32_t)fd, buff, (uint32_t)size); }
/* S2 (save path + card admin) */
int sceMcWrite(int fd, const void *buff, int size) { return ps2mc_write((uint32_t)fd, buff, (uint32_t)size); }
/* the original is a wrapper around sceMcOpen(port, slot, name, 0x40) */
int sceMcMkdir(int port, int slot, const char *name) { return ps2mc_open((uint32_t)port, (uint32_t)slot, name, 0x40); }
int sceMcFormat(int port, int slot) { return ps2mc_format((uint32_t)port, (uint32_t)slot); }
int sceMcUnformat(int port, int slot) { return ps2mc_unformat((uint32_t)port, (uint32_t)slot); }
int sceMcChdir(int port, int slot, const char *newDir, char *currentDir)
{
    return ps2mc_chdir((uint32_t)port, (uint32_t)slot, newDir, currentDir);
}
#endif
