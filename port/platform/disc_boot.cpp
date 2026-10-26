/* Launch-time disc setup (see disc_boot.h). */
#include "disc_boot.h"
#include "disc.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static int is_abs(const char *p)
{
    return p[0] == '\\' || p[0] == '/' || (isalpha((unsigned char)p[0]) && p[1] == ':');
}

extern "C" const char *disc_boot_path(const DiscIdent *id, const char *setting, const char *ini_dir,
                                      const char *root, char *out, size_t n)
{
    const char *e = getenv("ACNB_ISO");
    if (e && *e) snprintf(out, n, "%s", e);
    else if (setting && *setting) {
        if (is_abs(setting) || !ini_dir || !*ini_dir) snprintf(out, n, "%s", setting);
        else snprintf(out, n, "%s\\%s", ini_dir, setting);
    } else snprintf(out, n, "%s\\%s", root, id->default_image);
    return out;
}

extern "C" int disc_boot_open(const DiscIdent *id, const char *path, uint8_t **elf, uint32_t *elf_size, char *msg,
                              size_t msgn)
{
    disc_open(path);
    char m[400];
    int r = disc_id_check(id, elf, elf_size, m, sizeof m);
    if (r != DISCID_OK) {
        disc_close();
        snprintf(msg, msgn, "%s\n  disc image: %s", m, path);
    } else if (msgn) msg[0] = 0;
    return r;
}

extern "C" void disc_boot_error(const char *msg, int windowed)
{
    fprintf(stderr, "acnb: %s\n", msg);
    fflush(stderr);
#ifdef _WIN32
    if (windowed) MessageBoxA(0, msg, "Armored Core: Nine Breaker", MB_ICONERROR | MB_OK);
#else
    (void)windowed;
#endif
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int same_nocase(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
    return *a == *b;
}

extern "C" const char *ini_file_get(const char *file, const char *key, char *out, size_t n)
{
    FILE *f = fopen(file, "r");
    if (!f) return 0;
    char line[1024], section[32] = "";
    int found = 0;
    while (fgets(line, sizeof line, f)) {
        char *s = trim(line);
        if (!*s || *s == ';' || *s == '#') continue;
        if (*s == '[') {
            char *e = strchr(s, ']');
            if (e) { *e = 0; snprintf(section, sizeof section, "%s", trim(s + 1)); }
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(s), *v = trim(eq + 1), full[96];
        if (!*k) continue;
        if (*section && !strchr(k, '.')) snprintf(full, sizeof full, "%s.%s", section, k);
        else snprintf(full, sizeof full, "%s", k);
        if (!same_nocase(full, key)) continue;
        snprintf(out, n, "%s", v);
        found = 1;
    }
    fclose(f);
    return found ? out : 0;
}
