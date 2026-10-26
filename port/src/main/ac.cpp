/* src/main/ac.cpp - the disc type (s_disctype) the boot code finds in the root directory (__SearchRootFiles) and
 * the game reads back later. */
#include "acnb/port.h"

#ifdef ACNB_SOURCEPORT
int s_disctype = 1;   /* 0x27aeb8 */
#endif

void Ac_SetDiscType(int disctype) { s_disctype = disctype; }

int Ac_GetDiscType(void) { return s_disctype; }
