/* Names: the build backend + the name / type tables both debug engines share - the
 * sandbox (platform/sandbox: --script, pokes) and the state hash (platform/statehash). Split out of the sandbox in s103
 * so Stage 0 can keep names + statehash while stripping the script / poke engine. Plain C data (the hybrid's game.dll
 * links without the C++ library).
 *
 * Backend: one per build - hybrid runtime/sbx_hybrid.c (guest memory, PS2 addresses), source port
 * sourceport/main/host_sbx.cpp (host memory, module base + RVA). Tables: sbx_names.tsv / sbx_types.tsv beside the exe
 * (scripts/sbx_names.py, post-link step of both builds); the table's sentinels are checked against the running build
 * at load (a stale table is refused). Places:
 *   NAME                    symbol (unique name; a clash is a load error)
 *   FILE.c:NAME             the symbol of that source file (menu/msys.c:__PlaySystem or msys.c:__PlaySystem)
 *   @0xADDR                 the symbol whose PS2 address is ADDR (looked up in the table: valid in both builds)
 *   NAME[i].field[j]..      field path through the DWARF types (arrays / struct members, no pointer chase) */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct SbxBackend {
    const char *build;                            /* "hybrid" / "sourceport": must match the table's header */
    const char *table_dir;                        /* directory of sbx_names.tsv / sbx_types.tsv */
    /* host pointer to n readable bytes at a (an address of this build), 0 = unmapped */
    const void *(*ptr)(uint32_t a, uint32_t n);
    uint32_t (*vblank)(void);                     /* the vblank counter (expressions' `vb`) */
    int (*overlay)(void);                         /* loaded overlay: 0 none, 1 OLSORTIE, 2 OLMENU */
    /* a table row's address in this build: ps2 = PS2 address, rva = the source port's RVA (has_rva 0 = '-');
     * 0 = the symbol is not in this build */
    uint32_t (*row_addr)(uint32_t ps2, uint32_t rva, int has_rva);
    /* this build's real address of a sentinel symbol named in the table header (0 = unknown -> table refused) */
    uint32_t (*sentinel)(const char *name);
    /* name of the function at a, for the objs listing (0: the engine looks it up in the table) */
    const char *(*fn_name)(uint32_t a);
    void (*log)(const char *line);                /* one log line, no newline */
    /* S1 (pokes): host pointer to n writable bytes at a, 0 = refused (hybrid: guest memory; source port: committed
     * PAGE_READWRITE pages only) */
    void *(*wptr)(uint32_t a, uint32_t n);
    const char *build_dir;                        /* port\build (the reference cards NAME_card_orig for `card NAME`) */
    /* CPI pins of the #acnb-sbx header: get = this run's values; set = a v3 replay's values before the game starts
     * (hybrid: ACNB_CPI / ACNB_IOP_CPI0, logged when it overrides; source port: no CPI model, logged only) */
    void (*get_pins)(int *ee_cpi, int *iop_cpi0);
    void (*set_pins)(int ee_cpi, int iop_cpi0);
} SbxBackend;

/* the build's backend (sbx_hybrid_init / sbx_sourceport_init set it; 0 until then) */
void sbx_set_backend(const SbxBackend *b);
const SbxBackend *sbx_backend(void);
extern const SbxBackend *sbx_be;

/* Name tables (loaded once; sbx_names_load is idempotent). err = static message on failure. */
int sbx_names_load(const SbxBackend *b, const char **err);
int sbx_names_ready(void);
/* NAME / FILE.c:NAME / @0xADDR -> address in this build; 0 = error */
int sbx_resolve(const char *name, uint32_t *addr, const char **err);
/* field path (NAME[i].f..; no pointer chase) -> address + encoded type (sbx_names.py: s32, s:NAME, aN:T, ..); 0 = error */
int sbx_place(const char *path, uint32_t *addr, const char **type, const char **err);
/* byte size of an encoded type (0 = unknown) */
uint32_t sbx_type_size(const char *type);

/* qual = FILE.c prefix (qn bytes) or 0 */
int sbx_lookup(const char *qual, size_t qn, const char *name, uint32_t *addr, const char **type, const char **err);
/* set by a failed sbx_lookup / sbx_lookup_ps2: 1 = the name is in the table but not in this build (not native in the
 * source port yet), 0 = any other error */
extern int sbx_absent;
/* set by a successful sbx_lookup / sbx_lookup_ps2: the row's overlay (0 main / none, 1 OLSORTIE, 2 OLMENU) */
extern int sbx_last_ovl;
int sbx_lookup_ps2(uint32_t ps2, uint32_t *addr, const char **type, const char **err);
const char *sbx_fn_name_at(uint32_t a);
/* field path steps after a symbol: index(&s, &err) parses one index expression and leaves s at ']' */
int sbx_walk(const char **p, uint32_t *addr, const char **type, const char **err, long (*index)(const char **, const char **));

/* statehash (s103): the symbol (function or data) whose range holds this build's address a, rows of the main ELF or
 * of overlay ovl (1 OLSORTIE, 2 OLMENU; others skipped). Range = the row size, else the DWARF type size, else the
 * address alone; nothing there -> the other overlay's data rows (menu data the sortie keeps, ComData). Same
 * answer in both builds (same rows). 0 = none; *off = a - symbol address */
const char *sbx_sym_at(uint32_t a, int ovl, uint32_t *off);
/* set by sbx_sym_at: another function row at the same address (source port identical-code folding: the name is
 * ambiguous, the first row in table order wins), 0 = none */
extern const char *sbx_sym_fold;
/* struct / union / enum by name: 1 = found; *first / *count = member range for sbx_member, *size, *is_enum */
int sbx_agg(const char *name, uint32_t *first, uint32_t *count, uint32_t *size, int *is_enum);
/* member i (from sbx_agg's range): name, byte offset, encoded type ("bf:BITS:BITOFF:T" for a bitfield) */
void sbx_member(uint32_t i, const char **name, uint32_t *off, const char **type);

#ifdef __cplusplus
}
#endif
