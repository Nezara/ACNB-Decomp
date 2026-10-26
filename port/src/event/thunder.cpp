/* src/event/thunder.cpp - lightning flashes of stormy stages. thunder_mng holds three light colour sets (normal,
 * flash, ambient) that EffectThunderLight copies into the stage lights every frame while the effect is on; the
 * flash state (lsw) switches light 0 to the static `lightning` colours, msw spawns the lightning bolt effect. */
#include "acnb/port.h"
#include "acnb/sdk.h"

void EffectThunderLight(_map_light_st *light)
{
    if (!thunder_mng.on) return;
    PS2_FRAME(0x40);
    sceVu0CopyMatrix(light[(int)thunder_mng.lid].col, thunder_mng.col[(int)thunder_mng.lsw]);
    sceVu0CopyMatrix(light->col, thunder_mng.lsw ? lightning : thunder_mng.col[2]);
    if (thunder_mng.msw && thunder_mng.magic_id) {
        PS2_STACK(float[4], scl, 0x30);
        scl[0] = scl[1] = scl[2] = scl[3] = 1.0f;
        DF_MAGIC2_AppearMagic(thunder_mng.magic_id, thunder_mng.magic_pos, thunder_mng.magic_ang, scl, 0, 0, 0, 0);
    }
}

void InitThunder(void)
{
    memset(&thunder_mng, 0, 0x100);
    thunder_mng.snd_wait = 0x78;
    thunder_mng.lit_wait = 300;
}
