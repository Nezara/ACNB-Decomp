/* Disc identity check (see disc_id.h) + the ACNB identity table. */
#include "disc_id.h"
#include "disc.h"
#include "sha1.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const DiscIdFile acnb_files[] = {
    {"\\OLMENU.BIN;1", "788de38323745c4dac15a585eb9a3a2b01d9f75e"},
    {"\\OLSORTIE.BIN;1", "c56c4533b19d3f4d870f8e5212543671694e2cad"},
};
/* every file a Versus run reads (disc traces of garage6 / battle1 / auto_arenarank1): IRX pack, sound banks, AC
 * parts, disc binder, the 9 ADX streams the sound init finds (ADS0009-0031 are searched and absent on the disc) */
static const char *const acnb_present[] = {
    "\\IOP\\IRX.BIN;1", "\\BMSS.DAT;1", "\\AC.BIN;1", "\\DISC.BIN;1",
    "\\ADS\\ADS0000.ADX;1", "\\ADS\\ADS0001.ADX;1", "\\ADS\\ADS0002.ADX;1", "\\ADS\\ADS0003.ADX;1",
    "\\ADS\\ADS0004.ADX;1", "\\ADS\\ADS0005.ADX;1", "\\ADS\\ADS0006.ADX;1", "\\ADS\\ADS0007.ADX;1",
    "\\ADS\\ADS0008.ADX;1",
};
extern "C" const DiscIdent disc_id_acnb = {
    "the USA release (SLUS-21200)", "cdrom0:\\SLUS_212.00;1", "84510ca247d7ebbe757574ddda7236806777a890",
    acnb_files, (int)(sizeof acnb_files / sizeof *acnb_files),
    acnb_present, (int)(sizeof acnb_present / sizeof *acnb_present),
    "Armored Core - Nine Breaker (USA).iso",
};

extern "C" uint8_t *disc_load_file(const char *path, uint32_t *size)
{
    uint32_t lsn, sz;
    if (!disc_find(path, &lsn, &sz)) return 0;
    uint32_t nsec = (sz + DISC_SECTOR - 1) / DISC_SECTOR;
    uint8_t *p = (uint8_t *)malloc(nsec ? (size_t)nsec * DISC_SECTOR : 1);
    if (!p) return 0;
    if (nsec && disc_read(lsn, nsec, p)) { free(p); return 0; }
    *size = sz;
    return p;
}

static void hash_hex(const uint8_t *p, uint32_t n, char hex[41])
{
    Sha1 s;
    uint8_t d[20];
    sha1_init(&s);
    sha1_update(&s, p, n);
    sha1_final(&s, d);
    sha1_hex(d, hex);
}

/* BOOT2 value of a SYSTEM.CNF text ("BOOT2 = cdrom0:\X;1"), trimmed; 0 = no BOOT2 line */
static int cnf_boot2(const char *t, uint32_t n, char *out, size_t outn)
{
    uint32_t i = 0;
    while (i < n) {
        uint32_t e = i;
        while (e < n && t[e] != '\n' && t[e] != '\r') e++;
        uint32_t k = i;
        while (k < e && (t[k] == ' ' || t[k] == '\t')) k++;
        if (e - k >= 5 && !strncmp(t + k, "BOOT2", 5)) {
            k += 5;
            while (k < e && (t[k] == ' ' || t[k] == '\t')) k++;
            if (k < e && t[k] == '=') {
                k++;
                while (k < e && (t[k] == ' ' || t[k] == '\t')) k++;
                uint32_t z = e;
                while (z > k && (t[z - 1] == ' ' || t[z - 1] == '\t')) z--;
                size_t len = z - k < outn - 1 ? z - k : outn - 1;
                memcpy(out, t + k, len);
                out[len] = 0;
                return 1;
            }
        }
        i = e + 1;
    }
    return 0;
}

static int same_nocase(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
    return *a == *b;
}

extern "C" int disc_id_check(const DiscIdent *id, uint8_t **elf, uint32_t *elf_size, char *msg, size_t msgn)
{
    if (disc_kind() == DISC_NONE) {
        snprintf(msg, msgn, "No disc image is set or it cannot be opened: set your disc image in the launcher "
                 "(or ACNB_ISO).");
        return DISCID_NO_DISC;
    }
    uint32_t n;
    uint8_t *cnf = disc_load_file("\\SYSTEM.CNF;1", &n);
    char boot[128];
    int have = cnf && cnf_boot2((const char *)cnf, n, boot, sizeof boot);
    free(cnf);
    if (!have) {
        snprintf(msg, msgn, "The disc image has no readable SYSTEM.CNF: set your disc image in the launcher.");
        return DISCID_NO_DISC;
    }
    if (!same_nocase(boot, id->boot2)) {
        const char *b = strrchr(boot, '\\');
        b = b ? b + 1 : boot;
        char name[64];
        snprintf(name, sizeof name, "%s", b);
        char *semi = strchr(name, ';');
        if (semi) *semi = 0;
        snprintf(msg, msgn, "This disc is %s; only %s is supported.", name, id->release);
        return DISCID_OTHER_GAME;
    }
    const char *path = strchr(id->boot2, ':');
    path = path ? path + 1 : id->boot2;
    char hex[41];
    uint32_t esz;
    uint8_t *e = disc_load_file(path, &esz);
    if (!e) {
        snprintf(msg, msgn, "The disc image's boot file %s cannot be read: set your disc image in the launcher.", path);
        return DISCID_NO_DISC;
    }
    hash_hex(e, esz, hex);
    if (strcmp(hex, id->boot_sha1)) {
        free(e);
        snprintf(msg, msgn, "Modified or unknown version of the game (%s sha1 %s); only %s is supported.", path, hex,
                 id->release);
        return DISCID_MODIFIED;
    }
    for (int i = 0; i < id->nfiles; i++) {
        uint8_t *f = disc_load_file(id->files[i].path, &n);
        if (f) hash_hex(f, n, hex);
        free(f);
        if (!f || strcmp(hex, id->files[i].sha1)) {
            free(e);
            if (!f) snprintf(msg, msgn, "Modified or unknown version of the game (%s is missing).", id->files[i].path);
            else snprintf(msg, msgn, "Modified or unknown version of the game (%s sha1 %s).", id->files[i].path, hex);
            return DISCID_MODIFIED;
        }
    }
    for (int i = 0; i < id->npresent; i++) {
        uint32_t lsn, sz;
        if (disc_find(id->present[i], &lsn, &sz)) continue;
        free(e);
        snprintf(msg, msgn, "The disc image is incomplete (%s is missing): set a full image of %s in the launcher.",
                 id->present[i], id->release);
        return DISCID_MODIFIED;
    }
    if (elf) { *elf = e; *elf_size = esz; }
    else free(e);
    return DISCID_OK;
}
