/* lib/chunk.cpp — minimal chunk-file parser: chunks are {char id[4]; u32 size; 8 reserved; payload @0x10}. Each
 * chunk's id is looked up in the parser's id table and the payload is handed to the parser's callback, which
 * decides when to stop (returns 0). Used by SPRMNG_Init. */
#include "acnb/port.h"
#include "acnb/sdk.h"

/* Quirk (kept): the chunk id bytes are compared sign-extended (lb) against the table bytes zero-extended (lbu), so
 * an id byte >= 0x80 never matches. id_max / id_tab are re-read for every chunk; the next chunk address is taken
 * before the callback runs. */
void CNK_ParseData(struct_1b9d99a9 *psr, void *data)
{
    PS2_FRAME(0x30);
    char *ptr = (char *)data;
    int r;
    do {
        struct_4e589b19 *hdr = (struct_4e589b19 *)ptr;
        int id_max = psr->id_max;
        char **id_tab = psr->id_tab;
        ptr += hdr->size;
        int idx = -1;
        for (int i = 0; i < id_max; i++) {
            const signed char *c = (const signed char *)hdr;
            const unsigned char *t = (const unsigned char *)id_tab[i];
            if (c[0] == t[0] && c[1] == t[1] && c[2] == t[2] && c[3] == t[3]) {
                idx = i;
                break;
            }
        }
        r = gcall(psr->parse, idx, (void *)((char *)hdr + 0x10), psr->paramp);
    } while (r != 0);
}

void CNK_CreateParser(struct_1b9d99a9 *psr, char **id_tab, int id_max, int (*parse)(int, void *, void *), void *paramp)
{
    psr->id_tab = id_tab;
    psr->id_max = id_max;
    psr->parse = parse;
    psr->paramp = paramp;
}
