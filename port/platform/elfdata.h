/* Data tables copied out of the game's boot ELF at launch (phase 3 2.5): the source port keeps original binary
 * tables (sine table, random tables, records) out of the exe and fills their storage from the user's disc. The table
 * of entries is generated per game (sourceport: scripts/sp_missing.py -> build/gen/stub_elfdata.cpp); this part only
 * knows ELF32 little-endian PT_LOAD segments. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct ElfDataEntry {
    const char *name;   /* for messages */
    void *dst;          /* host storage, `size` bytes */
    uint32_t vaddr;     /* address in the ELF image (inside a PT_LOAD segment's file-backed part) */
    uint32_t size;
    uint32_t crc;       /* crc32 (disc_crc32) of the bytes; catches a wrong vaddr / size in the table */
    const char *image;  /* 0 = the boot ELF; else a flat image file on the disc ("\OLSORTIE.BIN;1"), vaddr = its offset */
} ElfDataEntry;

/* a pointer word inside a copied table: the original's PS2 address replaced by the host object's */
typedef struct ElfDataReloc {
    void **at;
    void *to;
} ElfDataReloc;

/* copies every entry; 0 = ok, else the 1-based index of the first entry that failed (not in the image / crc), with
 * a message in err */
int elfdata_load(const uint8_t *elf, size_t elf_size, const ElfDataEntry *tab, int count, char *err, size_t errn);
/* patches the relocations (after elfdata_load) */
void elfdata_relocate(const ElfDataReloc *tab, int count);

#ifdef __cplusplus
}
#endif
