/* Disc identity (phase 3 2.6): which game / release the open disc is. Generic check over a per-game DiscIdent table
 * (SYSTEM.CNF BOOT2 line, SHA-1 of the boot ELF and of the files whose code the build runs); the ACNB table is
 * disc_id_acnb. Same check in both builds. The disc must be open (disc_open). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct DiscIdFile {
    const char *path;   /* disc path, "\\OLMENU.BIN;1" */
    const char *sha1;   /* lower-case hex */
} DiscIdFile;

typedef struct DiscIdent {
    const char *release;     /* for messages: "the USA release (SLUS-21200)" */
    const char *boot2;       /* expected SYSTEM.CNF BOOT2 value, "cdrom0:\\SLUS_212.00;1" */
    const char *boot_sha1;   /* SHA-1 of the boot ELF */
    const DiscIdFile *files; /* also checked at launch */
    int nfiles;
    const char *const *present;   /* must exist (not hashed): files the game loads from the start. A folder source
                                   * missing one would make the game retry the search forever */
    int npresent;
    const char *default_image;    /* image file name in the root folder when no path is set */
} DiscIdent;

/* results = distinct process exit codes headless */
enum { DISCID_OK = 0, DISCID_NO_DISC = 2, DISCID_OTHER_GAME = 3, DISCID_MODIFIED = 4 };

/* checks the open disc against id (SYSTEM.CNF BOOT2, boot ELF sha1, files[] sha1, present[] exist); on DISCID_OK and elf != 0, *elf = malloc'd boot ELF (caller frees), *elf_size
 * its size. msg gets a user-facing sentence for every non-OK result. */
int disc_id_check(const DiscIdent *id, uint8_t **elf, uint32_t *elf_size, char *msg, size_t msgn);

/* malloc'd copy of a disc file (whole sectors read, `*size` = file size); 0 = not found / read error */
uint8_t *disc_load_file(const char *path, uint32_t *size);

extern const DiscIdent disc_id_acnb;   /* Armored Core: Nine Breaker, USA SLUS-21200 (VER 1.03) */

#ifdef __cplusplus
}
#endif
