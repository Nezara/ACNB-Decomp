/* src/enemy/einfo.cpp (OLSORTIE) - enemy bookkeeping for the mission-info store: EINFO_SetEnemyCountToSINFO
 * publishes the enemy counts (StModel cflag bit 0 set / clear) to sinfo, DF_EINFO_Init clears the per-group destroy
 * counters, DF_EINFO_GrpDestroyCnt (garage3 layer 2) counts a kill for the enemy's group (saturating at 0xff).
 * The group / remain / HP-rate queries stay interpreted. */
#include "acnb/port.h"

void EINFO_SetEnemyCountToSINFO(void)
{
    PS2_FRAME(0x20);
    PS2_STACK(int, a, 0x1c);   /* both counters live in their stack slots (DWARF union locals) */
    PS2_STACK(int, b, 0x18);
    a = 0;
    b = 0;
    EnemySt *e = Enemy;
    for (int i = 0; i < EnemyNum; i++, e++) {
        if (e->mp->cflag & 1) a++;
        else b++;
    }
    /* the union value goes in t0 (5th integer argument) */
    gcall_as<void(int, int, int, int, int)>(GFN(Info_SetData), 3, 2, 0, 0, a);
    gcall_as<void(int, int, int, int, int)>(GFN(Info_SetData), 3, 3, 0, 0, b);
    gcall_as<void(int, int, int, int, int)>(GFN(Info_SetData), 3, 6, 0, 0, a);
    gcall_as<void(int, int, int, int, int)>(GFN(Info_SetData), 3, 7, 0, 0, b);
}

void DF_EINFO_Init(void)
{
    ZeroMemory32b(EneGrpDestroyCnt, 8);   /* tail call, no frame */
}

void DF_EINFO_GrpDestroyCnt(EnemySt *pEne)
{
    int grp = pEne->mp->array->ene_grpid;
    if (grp < 0x20) {
        int c = EneGrpDestroyCnt[grp];
        if (c < 0xff) EneGrpDestroyCnt[grp] = (unsigned char)(c + 1);
    }
}
