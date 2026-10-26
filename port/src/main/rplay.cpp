/* src/main/rplay.cpp — the replay key stream: one flat buffer of 4-byte input packets, one per player per frame.
 * DF_RPLAY_Init sets it up (record mode clears the buffer and counts up, playback mode starts from the recorded size and
 * counts down), DF_RPLAY_Count advances the byte counter once per frame, Get / Set / NextData read / write / advance each
 * player's packet pointer in s_plykey[].
 *
 * ABI: DF_RPLAY_Count gets (FRAME, rmode, proceed) in a0..a2; a0 is unused, so MWCC left it out of the DWARF (protofix). */
#include "acnb/sdk.h"

int DF_RPLAY_Count(int frame, int rmode, int proceed)
{
    (void)frame;
    if (proceed == 0) return 0;
    if (rmode != 0) {
        /* playback: ReplaySizeAdd is negative */
        if (ReplaySize > 0) {
            ReplaySize += ReplaySizeAdd;
            return 1;
        }
        return -1;
    }
    if (ReplaySize < ReplayBuffSize) {
        ReplaySize += ReplaySizeAdd;
        ReplaySizeBack = ReplaySize;
        return 1;
    }
    return 0;
}

/* (sic) no bound check against the buffer end */
void DF_RPLAY_NextData(int player)
{
    s_plykey[player] += ReplayPacketSize;
}

void DF_RPLAY_SetData(int player, void *data)
{
    memcpy(s_plykey[player], data, 4);
}

void *DF_RPLAY_GetData(int player)
{
    return s_plykey[player];
}

/* (sic) playback mode does not clear the buffer; the key pointers are rebuilt from buff on every call */
void DF_RPLAY_Init(int mode, int player_num, void *buff, int buff_size)
{
    ReplayPacketSize = player_num * 4;
    if (mode == 0) {
        ReplaySize = 0;
        ReplaySizeAdd = ReplayPacketSize;
        ZeroMemory32b(buff, (unsigned int)buff_size >> 2);
    } else {
        ReplaySize = ReplaySizeBack;
        ReplaySizeAdd = -ReplayPacketSize;
    }
    ReplayBuffSize = buff_size;
    ReplayBuffP = buff;
    char *p = (char *)buff;
    for (int i = 0; i < player_num; i++, p += 4) s_plykey[i] = p;
}
