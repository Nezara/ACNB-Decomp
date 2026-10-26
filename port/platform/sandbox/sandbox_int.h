/* Sandbox internals shared by script.cpp and events.cpp (not part of the backend interface). */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "sandbox.h"

/* names.cpp moved to platform/names (names.h, included by sandbox.h) */

/* script.cpp */
/* name table + the menu-object symbols obj() / scene() use (script load and v3 replay load); 1 = ok */
int sbx_engine_init(const char **err);
/* one expression over this build's memory; lint = resolve names only (nothing read, raw address literals refused).
 * *ovl = the overlay the named symbols need (0 none, 1 OLSORTIE, 2 OLMENU, -1 symbols of both). 1 = ok */
int sbx_eval(const char *e, int lint, double *out, const char **err, int *ovl);

/* events.cpp (pokes, freezes, v3 header, event player) */
uint32_t sbx_scalar_size(const char *type);   /* s8 u8 s16 u16 s32 u32 s64 u64 f32; 0 = not a poke type */
/* the script (load time): the expanded script's raw lines (loc = "N" / "NAME:N") */
void sbx_hdr_line(const char *loc, const char *text);
/* the script (load time): name, declared card ("-" = none), sandbox = it uses poke / freeze / unfreeze / card */
void sbx_hdr_script(const char *name, const char *card, int sandbox);
/* first poll point of the run: hashes the card, checks it (script: against the declared reference card; replay:
 * against the header), writes the deferred v3 header. 0 = ok, else the error */
const char *sbx_ev_begin(void);
/* one event "poke T EXPR BITS" / "freeze ID T EXPR BITS" / "unfreeze ID|all": lint = check only; run = apply it
 * (recorded as "x VB TEXT" when rec != 0). 1 = ok, 0 = *err */
int sbx_ev_do(const char *text, uint32_t vb, int lint, FILE *rec, const char **err);
int sbx_ev_freeze_active(int id);
int sbx_ev_failed(void);
