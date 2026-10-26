/* src/enemy/enelgc.cpp (OLSORTIE) - enemy pilot "fake" (feint) values: EneLgic_GetFakePrec / GetFakeFreq look up
 * the precision / frequency for the logic record's fake_id (high / low nibble) and the difficulty type in two small
 * 16 x 3 tables (ENEMY_AllEnemyInit stores them in ep->fake_value_prec / freq). */
#include "acnb/port.h"

#ifdef ACNB_SOURCEPORT
short s_FakePrecTable[16][3] = {
    {0, 0, 0},       {0, 50, 50},     {0, 50, 75},      {50, 50, 50},
    {50, 75, 75},    {50, 75, 100},   {75, 75, 75},     {75, 100, 100},
    {75, 100, 125},  {100, 100, 100}, {100, 125, 125},  {100, 125, 150},
    {125, 125, 125}, {125, 150, 50},  {175, 200, 225},  {200, 225, 250},
};
short s_FakeFreqTable[16][3] = {
    {100, 100, 100}, {100, 90, 90}, {100, 90, 80}, {100, 90, 70},
    {100, 80, 80},   {100, 80, 70}, {100, 80, 60}, {100, 75, 75},
    {100, 75, 50},   {100, 70, 70}, {100, 70, 60}, {100, 70, 50},
    {100, 60, 60},   {100, 60, 50}, {100, 60, 40}, {100, 50, 50},
};
#endif

short EneLgic_GetFakeFreq(unsigned char id, unsigned char type)
{
    return s_FakeFreqTable[id & 0xf][type];
}

short EneLgic_GetFakePrec(unsigned char id, unsigned char type)
{
    return s_FakePrecTable[(id >> 4) & 0xf][type];
}
