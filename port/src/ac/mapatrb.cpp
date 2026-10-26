/* src/ac/mapatrb.cpp - floor / water attribute records (whole file): footstep / landing / splash effect ids, SE ids
 * and hit data for an attribute byte 0..11. s_MapAtrbInfo (12 x 0x42 bytes) is game data: the source port copies it
 * from the user's ELF at build time (scripts/sp_missing.py ELF_DATA), the hybrid uses the PS2 copy. */
#include "acnb/port.h"

MapAttributeInfoSt *mapatrb_GetInfo(unsigned char atr)
{
    unsigned int a = atr & 0xff;
    return a < 12 ? &s_MapAtrbInfo[a] : NULL;
}
