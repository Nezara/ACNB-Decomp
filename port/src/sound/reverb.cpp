/* src/sound/reverb.cpp - stage ("environment") reverb for the event system: the same preset on both SPU2 cores
 * (Sm->rid[1] core 1, rid[2] core 0), removal / re-activation, and the four reverb-volume registers. */

static inline void reverb_remove(int k)
{
    int r = Sm->rid[k];
    if (0 <= r && r < 4) {
        fssdsynthRemoveReverb(r);
        Sm->rid[k] = -1;
    }
}

static inline void reverb_activate(int k)
{
    int r = Sm->rid[k];
    if (0 <= r && r < 4) fssdsynthSetReverb(r);
}

void DF_SND_SetRevVolume(int revvol)
{
    PS2_FRAME(0x20);
    fssdsynthSetParam(0xb80, (unsigned short)revvol);
    fssdsynthSetParam(0xc80, (unsigned short)revvol);
    fssdsynthSetParam(0xb81, (unsigned short)revvol);
    fssdsynthSetParam(0xc81, (unsigned short)revvol);
}

void DF_SND_RemoveReveb(void)
{
    PS2_FRAME(0x10);
    reverb_remove(1);
    reverb_remove(2);
}

void DF_SND_ResumeReveb(void)
{
    PS2_FRAME(0x10);
    reverb_activate(1);
    reverb_activate(2);
}

void DF_SND_SetReverb(int mode, short ldepth, short rdepth, short delay, short feedback, short lvol, short rvol)
{
    PS2_FRAME(0x80);
    if (!Sm) return;
    reverb_remove(1);
    reverb_remove(2);
    Sm->rid[1] = (char)fssdsynthAddReverb(mode, ldepth, rdepth, delay, feedback, lvol, rvol, 1);
    Sm->rid[2] = (char)fssdsynthAddReverb(mode, ldepth, rdepth, delay, feedback, lvol, rvol, 0);
    reverb_activate(1);
    reverb_activate(2);
}
