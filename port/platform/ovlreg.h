/* Overlay globals in a build without an overlay area (phase 3 2.8, source port). On the PS2 every overlay load
 * re-initialises that overlay's .data from the image and zeroes its .bss; the source port links all overlays' code at
 * once, so each overlay's globals live in their own linker-grouped ranges instead: the build places every global of
 * an overlay's source files (incl. file / function statics and zero-filled stand-ins) in .data$ovl_<NAME>_m /
 * .bss$ovl_<NAME>_m (clang `#pragma clang section`, sp preludes) between generated _a / _z marker objects. At start-up
 * ovl_init snapshots each .data range (the C++ initial values, after static constructors); ovl_restore puts the
 * snapshot back and zeroes the .bss range - the overlay's state right after a PS2 load. Read-only data is not
 * covered (never written). Generic: overlay names come from the generated table. */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct OvlImage {
    const char *name;               /* "OLMENU" = the image file's base name */
    uint8_t *data_lo, *data_hi;     /* [lo, hi): initialised globals */
    uint8_t *bss_lo, *bss_hi;       /* [lo, hi): zero-initialised globals */
} OvlImage;

int ovl_init(const OvlImage *imgs, int n);   /* snapshots; 0 = ok, -1 = out of memory / bad range */
int ovl_restore(const char *name);           /* 1 = restored, 0 = unknown overlay */
/* restore by image path ("cdrom0:\\OLMENU.BIN;1" -> "OLMENU"): base name up to the first '.' / ';' */
int ovl_restore_path(const char *path);
/* name of the overlay restored last (the loaded one), 0 = none yet */
const char *ovl_current(void);
int ovl_verify(const char *name);            /* 1 = the overlay's globals equal the post-load state */
/* data / bss byte counts of an overlay; 0 = unknown */
int ovl_sizes(const char *name, uint32_t *data, uint32_t *bss);

#ifdef __cplusplus
}
#endif
