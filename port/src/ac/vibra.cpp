/* src/ac/vibra.cpp - per-AC pad vibration: Vibra_SetPower starts a vibration of `pow` for `time` frames, Vibra_Update
 * (once per frame from __ExecAc) counts the frames and drives the pad motor through the fspad layer. */
#include <string.h>

void Vibra_StopAll(void)
{
    PS2_FRAME(0x10);
    fspad_PadSetVibra(0, 0, 0);
    fspad_PadSetVibra(1, 0, 0);
}

unsigned char Vibra_Update(struct struct_16666bbd *vmngp)
{
    PS2_FRAME(0x20);
    if ((unsigned int)vmngp->padno >= 2) return 0;
    unsigned char pow = vmngp->pow;
    short nframe;
    if (pow == 0 || (nframe = vmngp->nframe) == 0 || !(vmngp->frame < nframe)) {
        pow = 0;
    } else {
        vmngp->frame = (short)(vmngp->frame + 1);
        if (!(vmngp->frame < nframe)) vmngp->pow = 0;   /* the last frame still vibrates at pow */
    }
    fspad_PadSetVibra(vmngp->padno, 0, pow);
    return pow;
}

void Vibra_SetPower(struct struct_16666bbd *vmngp, unsigned char pow, short time)
{
    vmngp->frame = 0;
    vmngp->nframe = time;
    vmngp->pow = pow;
}

void Vibra_Init(struct struct_16666bbd *vmngp, int padno)
{
    memset(vmngp, 0, 0x20);
    vmngp->padno = padno;
}
