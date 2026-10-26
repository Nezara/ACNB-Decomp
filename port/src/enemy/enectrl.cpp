/* src/enemy/enectrl.cpp (OLSORTIE) - enemy control helpers. garage3 layer 1: EneCtrl_RegistECM registers an enemy's
 * ECM emitter (EnemyDataSt.ecm_level) with the stage ECM field (stage/ecmfield); layer 2: EneCtrl_UnRegistECM drops it
 * again (enemy death). The roll / pitch helpers and the rest of the file stay interpreted for now. */
#include "acnb/port.h"

unsigned char EneCtrl_RegistECM(EnemySt *pEne)
{
    PS2_FRAME(0x20);
    short lvl = pEne->edat->ecm_level;
    if (lvl == 0) return 0;
    if (pEne->ecm_register_id != 0) return 0;   /* already registered: 0, not the id */
    pEne->ecm_register_id = EcmField_RegisterEcm(4, pEne->ecm_belonging, (short)pEne->mp->aid, (unsigned short)lvl);
    return pEne->ecm_register_id;
}

void EneCtrl_UnRegistECM(EnemySt *pEne)
{
    PS2_FRAME(0x20);
    if (pEne->ecm_register_id) {
        EcmField_UnRegisterEcm(pEne->ecm_register_id);
        pEne->ecm_register_id = 0;
    }
}
