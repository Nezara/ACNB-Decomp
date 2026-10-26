/* lib/host.cpp - host0: (dev kit) file access of the final game: cut down to a constant stub. Its one caller is
 * File_IsExistNM with lmode 1, which no game code uses. */
#include "acnb/port.h"

int Host_IsFileExist(void)
{
    return 0;
}
