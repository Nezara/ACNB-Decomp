/* Source-port launch (see sp_boot.h). The generated tables: build/gen/stub_elfdata.cpp (sp_elfdata) and
 * stub_ovlreg.cpp (sp_overlays), written by scripts/sp_missing.py. */
#include "sp_boot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../../port/platform/disc.h"
#include "../../port/platform/disc_boot.h"
#include "../../port/platform/disc_id.h"
#include "../../port/platform/elfdata.h"
#include "../../port/platform/ovlreg.h"
#include "../../port/platform/ps2_file.h"
#include "../../port/platform/ps2_mc.h"

extern "C" const ElfDataEntry sp_elfdata[];
extern "C" const int sp_elfdata_count;
extern "C" const ElfDataReloc sp_elfreloc[];
extern "C" const int sp_elfreloc_count;
extern "C" const ElfDataReloc sp_elfreloc[];
extern "C" const int sp_elfreloc_count;
extern "C" const OvlImage sp_overlays[];
extern "C" const int sp_overlay_count;

char sp_root_dir[600];

static void root_dir(void)
{
    char exe[600];
    DWORD n = GetModuleFileNameA(0, exe, sizeof exe);
    if (!n || n >= sizeof exe) { snprintf(sp_root_dir, sizeof sp_root_dir, "."); return; }
    for (int up = 0; up < 3; up++) {   /* acnb.exe -> build -> sourceport -> root */
        char *s = strrchr(exe, '\\');
        if (s) *s = 0;
    }
    snprintf(sp_root_dir, sizeof sp_root_dir, "%s", exe);
}

/* acnb.ini: next to acnb.exe if there is one, else the hybrid / launcher's <root>\port\build\acnb.ini (the launcher
 * sets disc.path there once for both builds). Read-only: the sourceport never writes it. */
static void find_ini(char *ini, size_t n, char *dir, size_t dn)
{
    char exe[600];
    DWORD k = GetModuleFileNameA(0, exe, sizeof exe);
    char *s = k && k < sizeof exe ? strrchr(exe, '\\') : 0;
    if (s) {
        *s = 0;
        snprintf(ini, n, "%s\\acnb.ini", exe);
        if (GetFileAttributesA(ini) != INVALID_FILE_ATTRIBUTES) { snprintf(dir, dn, "%s", exe); return; }
    }
    snprintf(dir, dn, "%s\\port\\build", sp_root_dir);
    snprintf(ini, n, "%s\\acnb.ini", dir);
}

/* Stage 0 disc default (no ACNB_ISO, no disc.path): the folder `game` one level below the root, <root>\game\<the
 * expected image name> ("Armored Core - Nine Breaker (USA).iso"); when that file is not there, the first *.iso in
 * game\ (the SHA-1 identity check decides either way). */
static void game_iso(char *out, size_t n)
{
    /* plain file lookup: a hard link and a real copy of the image are the same here */
    snprintf(out, n, "%s\\game\\%s", sp_root_dir, disc_id_acnb.default_image);
    DWORD at = GetFileAttributesA(out);
    if (at != INVALID_FILE_ATTRIBUTES && !(at & FILE_ATTRIBUTE_DIRECTORY)) return;
    char pat[700];
    snprintf(pat, sizeof pat, "%s\\game\\*.iso", sp_root_dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;   /* nothing there: the error names the expected path */
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            snprintf(out, n, "%s\\game\\%s", sp_root_dir, fd.cFileName);
            break;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

int sp_boot(void)
{
    root_dir();
    char host[700], iso[700], ini[700], ini_dir[700], setting[600];
    snprintf(host, sizeof host, "%s\\port\\host", sp_root_dir);
    ps2f_init(host);
    ps2mc_init(sp_root_dir);
    find_ini(ini, sizeof ini, ini_dir, sizeof ini_dir);
    const char *dp = ini_file_get(ini, "disc.path", setting, sizeof setting);
    const char *env = getenv("ACNB_ISO");
    if ((env && *env) || (dp && *dp)) disc_boot_path(&disc_id_acnb, dp, ini_dir, sp_root_dir, iso, sizeof iso);
    else game_iso(iso, sizeof iso);

    char msg[900];
    uint8_t *elf = 0;
    uint32_t elf_size = 0;
    int r = disc_boot_open(&disc_id_acnb, iso, &elf, &elf_size, msg, sizeof msg);
    if (r != DISCID_OK) {
        disc_boot_error(msg, 0);   /* no window yet (null backend): stderr + exit code only */
        return r;
    }
    if (disc_kind() == DISC_FOLDER) fprintf(stderr, "acnb: folder source %s (virtual LSNs)\n", iso);
    r = elfdata_load(elf, elf_size, sp_elfdata, sp_elfdata_count, msg, sizeof msg);
    free(elf);
    if (r) {
        fprintf(stderr, "acnb: %s\n", msg);
        return 5;
    }
    elfdata_relocate(sp_elfreloc, sp_elfreloc_count);   /* before ovl_init: overlay tables are snapshotted relocated */
    if (ovl_init(sp_overlays, sp_overlay_count)) {
        fprintf(stderr, "acnb: overlay globals snapshot failed\n");
        return 5;
    }
    return 0;
}
