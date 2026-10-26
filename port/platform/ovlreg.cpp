/* Overlay globals snapshot / restore (see ovlreg.h). */
#include "ovlreg.h"
#include "plat.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define OVL_MAX 8

static struct {
    OvlImage img;
    uint8_t *snap;   /* copy of [data_lo, data_hi) at ovl_init */
} ovl[OVL_MAX];
static int novl;

static int find(const char *name)
{
    for (int i = 0; i < novl; i++) {
        const char *a = ovl[i].img.name, *b = name;
        while (*a && toupper((unsigned char)*a) == toupper((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) return i;
    }
    return -1;
}

extern "C" int ovl_init(const OvlImage *imgs, int n)
{
    for (int i = 0; i < novl; i++) free(ovl[i].snap);
    novl = 0;
    if (n > OVL_MAX) return -1;
    for (int i = 0; i < n; i++) {
        const OvlImage *m = &imgs[i];
        if (m->data_hi < m->data_lo || m->bss_hi < m->bss_lo) return -1;
        size_t sz = (size_t)(m->data_hi - m->data_lo);
        uint8_t *s = (uint8_t *)malloc(sz ? sz : 1);
        if (!s) return -1;
        memcpy(s, m->data_lo, sz);
        ovl[novl].img = *m;
        ovl[novl].snap = s;
        novl++;
    }
    return 0;
}

static int cur = -1;   /* the overlay restored last (= loaded; the overlay area holds one at a time) */

extern "C" int ovl_restore(const char *name)
{
    int i = find(name);
    if (i < 0) return 0;
    const OvlImage *m = &ovl[i].img;
    memcpy(m->data_lo, ovl[i].snap, (size_t)(m->data_hi - m->data_lo));
    memset(m->bss_lo, 0, (size_t)(m->bss_hi - m->bss_lo));
    cur = i;
    return 1;
}

extern "C" const char *ovl_current(void) { return cur >= 0 ? ovl[cur].img.name : 0; }

extern "C" int ovl_restore_path(const char *path)
{
    const char *b = path;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':') b = p + 1;
    char name[64];
    size_t k = 0;
    while (b[k] && b[k] != '.' && b[k] != ';' && k < sizeof name - 1) { name[k] = b[k]; k++; }
    name[k] = 0;
    int ok = ovl_restore(name);
    if (!ok) plat_log("ovlreg: no registered overlay for %s", path);
    return ok;
}

extern "C" int ovl_verify(const char *name)
{
    int i = find(name);
    if (i < 0) return 0;
    const OvlImage *m = &ovl[i].img;
    if (memcmp(m->data_lo, ovl[i].snap, (size_t)(m->data_hi - m->data_lo))) return 0;
    for (const uint8_t *p = m->bss_lo; p < m->bss_hi; p++)
        if (*p) return 0;
    return 1;
}

extern "C" int ovl_sizes(const char *name, uint32_t *data, uint32_t *bss)
{
    int i = find(name);
    if (i < 0) return 0;
    *data = (uint32_t)(ovl[i].img.data_hi - ovl[i].img.data_lo);
    *bss = (uint32_t)(ovl[i].img.bss_hi - ovl[i].img.bss_lo);
    return 1;
}
