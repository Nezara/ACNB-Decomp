/* src/main/env.cpp - host path conversion of the PC tool build; on the PS2 the path is used as is. */
#include "acnb/port.h"

/* 0x1dd1c0 (8 B): jr ra; move v0, a0 - identity on the PS2 */
char *Env_ConvertIrxPath(char *pcPath) { return pcPath; }

/* 0x1dd1d0 (8 B): jr ra; move v0, a0 - identity on the PS2 */
char *Env_ConvertPath(char *pcPath) { return pcPath; }
