/* src/main/commsg.cpp - in-sortie "system messages" (OLSORTIE): radio text + voice id for a message id. The text comes
 * from the 22-entry pointer table s_tblNormalData, the voice id from s_tblSoundIdTop[id range][maker][cpu grade],
 * selected by the player AC's head part. Native: ComMsg_GetSystemMessage, __GetMessageSoundId (ComMsg_GetSoundId
 * stays interpreted). */
#include "acnb/port.h"

#ifdef ACNB_SOURCEPORT
/* [range 1..99 / 101..299 / 301..1399][maker 0 / 1][cpu grade 0..2]; 0 = no voice */
struct StMsgToSoundId s_tblSoundIdTop[3] = {
    {{{0x7d0, 0x834, 0x960}, {0xaf0, 0xb54, 0xc80}}},
    {{{0, 0x898, 0x9c4}, {0, 0xbb8, 0xce4}}},
    {{{0, 0, 0xa8c}, {0, 0, 0xdac}}},
};
#endif

static unsigned short __GetMessageSoundId(short idMsg)
{
    struct AcPlayerSt *pac = &g_ac[0];   /* (the asm's `if (!pac) return 0` is a constant test) */
    struct PartsParamHeadSt *head = pac->param.headp;
    if (!head) return 0;
    int co = head->basic.maker;
    if (co < 0 || co >= 2) co = 0;
    int grade = head->cpu_grade;
    if (grade < 0 || grade >= 3) grade = 0;
    int off = 0;
    unsigned int base = 0;
    if (idMsg > 0 && idMsg < 100) {
        off = idMsg - 1;
        base = s_tblSoundIdTop[0].idSnd[co][grade];
    } else if (idMsg >= 101 && idMsg < 300) {
        off = idMsg - 0x65;
        base = s_tblSoundIdTop[1].idSnd[co][grade];
    } else if (idMsg >= 301 && idMsg < 0x578) {
        off = idMsg - 0x12d;
        base = s_tblSoundIdTop[2].idSnd[co][grade];
    }
    if (base == 0) return 0;
    return (unsigned short)(base + off);
}

int ComMsg_GetSystemMessage(struct StComMsgUnit *pcmunit, short idMsg)
{
    PS2_FRAME(0x40);
    /* quirk (original bug, kept): ids 1..99 are accepted but the table has 22 pointers (+2 padding words); ids past
     * it read the following guest data as the text pointer (only stored when the voice id is nonzero) */
    struct StComMsgData *p = 0;
    if (idMsg > 0 && idMsg < 100) {
        struct StComMsgData *tbl = s_tblNormalData;
#ifdef ACNB_SOURCEPORT
        static struct StComMsgData none;   /* no guest data past the 22 entries here */
        p = idMsg - 1 < 22 ? &tbl[idMsg - 1] : &none;
#else
        p = tbl + (idMsg - 1);
#endif
    }
    if (!p) return 0;
    ZeroMemory(pcmunit, 8);
    pcmunit->idSnd = __GetMessageSoundId(idMsg);
    if (pcmunit->idSnd) pcmunit->pstrMsg = p->pstrMsg;
    return 1;
}
