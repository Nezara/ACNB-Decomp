/* Sandbox backend of the source port (port/platform/sandbox/sandbox.h): host memory (i686: game pointers are host
 * addresses, structs keep the PS2 layout), table rows at module base + RVA (sbx_names.tsv beside acnb.exe, written by
 * sourceport\build.ps1 after the link from acnb.map), loads checked with VirtualQuery (unmapped / no access = 0, as
 * the hybrid's unmapped guest memory). Sentinels: symbols this file takes the address of itself, so a table from an
 * older link is refused. Overlay = the one platform/ovlreg restored last (Overlay_Load). */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../../port/platform/sandbox/sandbox.h"
#include "../../port/platform/ovlreg.h"
#include "sp_vsync.h"

extern char g_gamedata;    /* sentinels (scripts/sbx_names.py SENT_SP): addresses only */
extern char MenuObjTopp;

namespace {

uintptr_t g_base;
struct { uintptr_t lo, hi; int ok, w; } g_rgn;   /* last VirtualQuery region (w: PAGE_READWRITE) */

int readable(uintptr_t a)
{
    if (a >= g_rgn.lo && a < g_rgn.hi) return g_rgn.ok;
    MEMORY_BASIC_INFORMATION mi;
    if (!VirtualQuery((const void *)a, &mi, sizeof mi)) { g_rgn = {a, a + 1, 0, 0}; return 0; }
    DWORD pr = mi.Protect & 0xff;
    int ok = mi.State == MEM_COMMIT && !(mi.Protect & PAGE_GUARD) && pr != PAGE_NOACCESS && pr != PAGE_EXECUTE;
    g_rgn.lo = (uintptr_t)mi.BaseAddress;
    g_rgn.hi = g_rgn.lo + mi.RegionSize;
    g_rgn.ok = ok;
    g_rgn.w = ok && (pr == PAGE_READWRITE || pr == PAGE_EXECUTE_READWRITE);
    return ok;
}
int writable(uintptr_t a) { return readable(a) && g_rgn.w; }

const void *sp_ptr(uint32_t a, uint32_t n)
{
    if (!a || !n) return 0;
    if (!readable(a) || !readable((uintptr_t)a + n - 1)) return 0;
    return (const void *)(uintptr_t)a;
}
/* pokes (S1): committed read-write pages only (never code / read-only data) */
void *sp_wptr(uint32_t a, uint32_t n)
{
    if (!a || !n) return 0;
    if (!writable(a) || !writable((uintptr_t)a + n - 1)) return 0;
    return (void *)(uintptr_t)a;
}
/* no CPI model (native code): the hybrid's CPI 0 + zero-cost IOP are what it matches */
void sp_get_pins(int *ee, int *iop) { *ee = 0; *iop = 1; }
void sp_set_pins(int ee, int iop)
{
    fprintf(stderr, "sandbox: replay header pins ee_cpi=%d iop_cpi0=%d (the source port has no CPI model)%s\n", ee, iop,
            ee ? ": ee_cpi != 0 - the run may diverge" : "");
}
uint32_t sp_vb(void) { return sp_vblank(); }
int sp_ovl(void)
{
    const char *n = ovl_current();
    return !n ? 0 : !strcmp(n, "OLSORTIE") ? 1 : !strcmp(n, "OLMENU") ? 2 : 0;
}
uint32_t sp_row_addr(uint32_t ps2, uint32_t rva, int has_rva) { (void)ps2; return has_rva ? (uint32_t)(g_base + rva) : 0; }
uint32_t sp_sentinel(const char *name)
{
    if (!strcmp(name, "g_gamedata")) return (uint32_t)(uintptr_t)&g_gamedata;
    if (!strcmp(name, "MenuObjTopp")) return (uint32_t)(uintptr_t)&MenuObjTopp;
    if (!strcmp(name, "sp_exit")) return (uint32_t)(uintptr_t)&sp_exit;
    return 0;
}
/* one write per line: fprintf on the unbuffered stderr cost ~8 ms per state line headless (s103 ST2) */
void sp_log(const char *line)
{
    char b[8192];
    size_t n = strlen(line);
    if (n > sizeof b - 2) n = sizeof b - 2;
    memcpy(b, line, n);
    b[n++] = '\n';
    fwrite(b, 1, n, stderr);
}

}  // namespace

extern "C" void sbx_sourceport_init(void)
{
    static char dir[MAX_PATH];
    static SbxBackend b;
    HMODULE m = GetModuleHandleA(0);
    g_base = (uintptr_t)m;
    DWORD n = GetModuleFileNameA(m, dir, sizeof dir);   /* acnb.exe's directory = sourceport\build */
    while (n && dir[n - 1] != '\\' && dir[n - 1] != '/') n--;
    dir[n ? n - 1 : 0] = 0;
    b.build = "sourceport";
    b.table_dir = dir;
    b.ptr = sp_ptr;
    b.vblank = sp_vb;
    b.overlay = sp_ovl;
    b.row_addr = sp_row_addr;
    b.sentinel = sp_sentinel;
    b.fn_name = 0;   /* the engine's table lookup (host function address -> name) */
    b.log = sp_log;
    b.wptr = sp_wptr;
    static char bdir[MAX_PATH + 32];   /* port\build (reference cards) */
    snprintf(bdir, sizeof bdir, "%s\\..\\..\\port\\build", dir);
    b.build_dir = bdir;
    b.get_pins = sp_get_pins;
    b.set_pins = sp_set_pins;
    sbx_set_backend(&b);
}
