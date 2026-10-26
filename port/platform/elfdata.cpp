/* ELF_DATA launch-time copy (see elfdata.h). */
#include "elfdata.h"
#include "disc_id.h"
#include <stdlib.h>
#include "plat.h"
#include <stdio.h>
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* file offset of [va, va + n) in a PT_LOAD segment's file-backed bytes; -1 = not there */
static long long elf_offset(const uint8_t *elf, size_t sz, uint32_t va, uint32_t n)
{
    if (sz < 0x34 || memcmp(elf, "\177ELF", 4) || elf[4] != 1 || elf[5] != 1) return -1;
    uint32_t phoff = rd32(elf + 0x1c);
    uint16_t phent = rd16(elf + 0x2a), phnum = rd16(elf + 0x2c);
    for (uint32_t i = 0; i < phnum; i++) {
        size_t ph = (size_t)phoff + (size_t)i * phent;
        if (ph + 32 > sz) return -1;
        const uint8_t *p = elf + ph;
        uint32_t type = rd32(p), off = rd32(p + 4), seg = rd32(p + 8), filesz = rd32(p + 16);
        if (type != 1 || va < seg || (uint64_t)va + n > (uint64_t)seg + filesz) continue;
        uint64_t o = (uint64_t)off + (va - seg);
        if (o + n > sz) return -1;
        return (long long)o;
    }
    return -1;
}

extern "C" int elfdata_load(const uint8_t *elf, size_t elf_size, const ElfDataEntry *tab, int count, char *err, size_t errn)
{
    const char *img_name = 0;   /* flat image files (overlays): the last one loaded stays cached */
    uint8_t *img = 0;
    uint32_t img_size = 0;
    int bad = 0;
    for (int i = 0; i < count && !bad; i++) {
        const ElfDataEntry *e = &tab[i];
        const uint8_t *src;
        if (e->image) {
            if (!img_name || strcmp(img_name, e->image)) {
                free(img);
                img = disc_load_file(e->image, &img_size);
                img_name = e->image;
            }
            if (!img || (uint64_t)e->vaddr + e->size > img_size) {
                snprintf(err, errn, "data table %s (%s+%#x+%#x) is not on the disc", e->name, e->image, e->vaddr, e->size);
                bad = i + 1;
                break;
            }
            src = img + e->vaddr;
        } else {
            long long o = elf_offset(elf, elf_size, e->vaddr, e->size);
            if (o < 0) {
                snprintf(err, errn, "data table %s (%#x+%#x) is not in the boot ELF", e->name, e->vaddr, e->size);
                bad = i + 1;
                break;
            }
            src = elf + o;
        }
        uint32_t crc = disc_crc32(src, e->size);
        if (crc != e->crc) {
            snprintf(err, errn, "data table %s: crc %08x, expected %08x", e->name, crc, e->crc);
            bad = i + 1;
            break;
        }
        memcpy(e->dst, src, e->size);
    }
    free(img);
    return bad;
}

extern "C" void elfdata_relocate(const ElfDataReloc *tab, int count)
{
    for (int i = 0; i < count; i++) *tab[i].at = tab[i].to;
}
