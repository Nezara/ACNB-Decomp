/* src/main/myopt.cpp - per-player AC options (cockpit, colour, plus, level, view mode, key config) copied into the
 * sortie's option slots at sortie start. Local play takes them from g_gamedata[]; an i.LINK match (modeSortie 4) from
 * the options the consoles exchanged (vs_ac_info[]); the modem variant (mode 5, init_myopt_modem) stays interpreted. */
#include "acnb/port.h"
#include <string.h>

/* user slots: the exchanged options unless an AI drives the slot; COM slots with a com id < 100 get zeroed options
 * and the default key config (the com id index counts COM slots only) */
static void init_myopt_ilink(void)
{
    PS2_FRAME(0x60);
    char *com = play_info.com_id;
    for (int player = 0; player < NowPlayerNum; player++) {
        if (player < NowUserPlayerNum) {
            MyOptionSt *opt = Sortie_GetAcOptionForInitializer(player);
            if (UseAI[player] == 0) memcpy(opt, &vs_ac_info[player].option, 0x40);
        } else {
            MyOptionSt *opt = Sortie_GetAcOptionForInitializer(player);
            if (*com < 100) {
                memset(opt, 0, 0x40);
                Game_SetDefaultKeyConfig(&opt->keyconf);
            }
            com++;
        }
    }
}

void InitMyOption(void)
{
    PS2_FRAME(0x30);
    unsigned char mode = g_sortie.pconstant->modeSortie;
    if (mode == 4) {
        init_myopt_ilink();
    } else if (mode == 5) {
        init_myopt_modem();
    } else {
        StAcrtGameData *g = g_gamedata;
        for (int player = 0; player < NowPlayerNum; player++, g++) {
            MyOptionSt *opt = Sortie_GetAcOptionForInitializer(player);
            opt->cockpit = g->cockpit;
            opt->cockpitcol = g->cockpitcol;
            opt->plus = g->plus;
            opt->level = g->option.level;
            opt->view_mode = g->isUseCockpit;
            memcpy(&opt->keyconf, &g->option.keyconf, 0x30);
        }
    }
}
