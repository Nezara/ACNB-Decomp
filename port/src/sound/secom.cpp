/* src/sound/secom.cpp - the common sound-effect bank (bank port 4: weapons, boosters, hits) every sortie uses:
 * LoopSortie_Begin hands over the loaded SE bundle (HD / BD at the header's offsets), the bank id goes to Sm->secom. */

void DF_SND_Remove_SECOM(void)
{
    PS2_FRAME(0x10);
    int b = Sm->secom;
    if (0 <= b && b < 16) {
        fssdsynthRemoveBank(b);
        Sm->secom = -1;
    }
}

void DF_SND_Setting_SECOM(StBgmHead *bp)
{
    PS2_FRAME(0x10);
    Sm->secom = fssdsynthAddBankFromEE2(4, (char *)bp + bp->boffs[0], bp->bsize[0], (char *)bp + bp->boffs[1], bp->bsize[1], 0);
    while (!fssdsynthSifTransSync()) {
    }
    Sm->porttbl[Sm->secom] = 4;
}
