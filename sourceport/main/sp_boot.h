/* Source-port launch (phase 3 S6): disc, identity, ELF_DATA tables, overlay snapshots - before the game's main(). */
#pragma once

extern char sp_root_dir[600];   /* repo root (acnb.exe = <root>\sourceport\build\acnb.exe) */

/* opens the disc image (ACNB_ISO, else disc.path of acnb.ini, else <root>\Armored Core - Nine Breaker (USA).iso:
 * port/platform/disc_boot.h), checks it is the USA release
 * (port/platform/disc_id), loads the ELF_DATA tables from its boot ELF and snapshots the overlay globals.
 * 0 = ready; else a message went to stderr and the value is the process exit code (DISCID_* / 5 = table error) */
int sp_boot(void);
