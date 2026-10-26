/* src/sfx/sfunc.cpp - spell (weapon effect) helpers. Native so far: SPELL_Shooter2Model and Sfunc_GetShooterModel
 * (shooter type / id -> model, the same code compiled twice) and DF_SPELL_GetShooterUlockMask (the shooter's lock-on
 * mask for DF_get_nearest). */
#include "acnb/port.h"

/* type 2 = AC (id 0x7f = none), 1 = stage character; anything else NULL */
StModel *SPELL_Shooter2Model(int s_type, int s_id)
{
    if (s_type == 2 && s_id == 0x7f) {
        s_type = 0;
        s_id = 0;
    }
    if (s_type == 2) return g_ac[s_id].mp;
    if (s_type == 1) return Chr + s_id;
    return NULL;
}

/* same as SPELL_Shooter2Model (missile_fire's shooter lookup) */
StModel *Sfunc_GetShooterModel(int s_type, int s_id)
{
    if (s_type == 2 && s_id == 0x7f) {
        s_type = 0;
        s_id = 0;
    }
    if (s_type == 2) return g_ac[s_id].mp;
    if (s_type == 1) return Chr + s_id;
    return NULL;
}

/* the shooter model's cflag & 7, | 0x200 for an AC whose player record byte 0x78a lacks bit 2, | 0x3000; 0 without
 * a model (arg0p not NULL-checked) */
unsigned short DF_SPELL_GetShooterUlockMask(int s_type, int s_id)
{
    StModel *mp = SPELL_Shooter2Model(s_type, s_id);
    if (!mp) return 0;
    unsigned int mask = mp->cflag & 7;
    if (s_type == 2 && !(((unsigned char *)mp->arg0p)[0x78a] & 4)) mask = (mask | 0x200) & 0xffff;
    return (unsigned short)(mask | 0x3000);
}
