/* Launch-time disc setup (phase 3 S7), one copy for game.dll (hybrid), acnb.exe (sourceport) and acnb_launcher.exe:
 * which image to open, open + identity check with the user-facing message, how the message is shown.
 *
 * Which image (first that is set):
 *   1. env ACNB_ISO          (scripts / checks; an ISO file or an extracted-files folder)
 *   2. disc.path setting     ([disc] path in acnb.ini, or --set disc.path=... on the hybrid's command line);
 *                            a relative path is relative to the folder of acnb.ini
 *   3. <root>\<id->default_image>   ("Armored Core - Nine Breaker (USA).iso" next to port\ and sourceport\) */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "disc_id.h"
#ifdef __cplusplus
extern "C" {
#endif

/* resolves the image path into out (see above); setting may be 0 / "" (unset). Returns out. */
const char *disc_boot_path(const DiscIdent *id, const char *setting, const char *ini_dir, const char *root, char *out,
                           size_t n);

/* disc_open(path) + disc_id_check(id). DISCID_OK: the disc stays open, *elf / *elf_size as disc_id_check (elf may be
 * 0). Else the disc is closed and msg = the identity sentence + "\n  disc image: <path>". */
int disc_boot_open(const DiscIdent *id, const char *path, uint8_t **elf, uint32_t *elf_size, char *msg, size_t msgn);

/* shows a launch error: always stderr ("acnb: <msg>"); also a message box when windowed (never headless: a box
 * would block scripted runs) */
void disc_boot_error(const char *msg, int windowed);

/* value of "section.key" in an acnb.ini-style file as settings.c reads it (sections, "key = value", ; / # comments,
 * last one wins); 0 = file or key absent */
const char *ini_file_get(const char *file, const char *key, char *out, size_t n);

#ifdef __cplusplus
}
#endif
