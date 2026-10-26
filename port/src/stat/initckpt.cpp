/* src/stat/initckpt.cpp - cockpit view setup (whole file): loads the cockpit file (sprite scenes of the monitor
 * frame, its texture, the panel TP2 model), relocates the textures to the player's VRAM pages and draws the
 * monitor frame (scene objects 2..7 toggled by the display flags) and the panel. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

void InitCkpt_GetScreenColor(int *rgba, int id) { memcpy(rgba, screen_color[id], 16); }

void InitCkpt_GetPanelColor(int *rgba, int id) { memcpy(rgba, panel_color[id], 16); }

void change_cockpit_screen_color(int id)
{
    PS2_FRAME(0x20);
    SPRMNG_SetMasterColor(&sprmng, screen_color[id]);
    memcpy(ckpt_mng.rgba, panel_mcolor[id], 16);
}

void draw_cockpit(int paneloff, int maker, unsigned int dflags)
{
    PS2_FRAME(0x40);
    if (SPRMNG_SetSceneNo(&sprmng, ckpt_mng.sceno, 1)) {
        if (!(dflags & 0xf)) {
            SPRMNG_CopyObject(&sprmng, 2, 1, 2);
            SPRMNG_CopyObject(&sprmng, 3, 1, 3);
        }
        if (!(dflags & 0x40)) {
            SPRMNG_CopyObject(&sprmng, 4, 1, 4);
            SPRMNG_CopyObject(&sprmng, 5, 1, 5);
        }
        if (!(dflags & 0x20)) {
            SPRMNG_CopyObject(&sprmng, 6, 1, 6);
            SPRMNG_CopyObject(&sprmng, 7, 1, 7);
        }
    }
    SPRMNG_Draw(&sprmng);
    ckpt_mng.paneloff = (unsigned char)paneloff;
    ckpt_mng.maker = (unsigned char)maker;
    CKPT_Draw(&ckpt_mng);
}

void load_cockpit_tmx(int playerno) { DF_ReadTMXPointer_TSS(cockpit_tmx, 1, playerno << 10, playerno << 5); }

/* file 0x4a / 0x4b (type): +0x10 sprite scenes, +0x20 TMX texture, +0x30 panel TP2 model */
void init_cockpit(int plno, int type)
{
    PS2_FRAME(0x60);
    char *buff = (char *)Memm_Alloc(1, 0x2c000, 0x10);
    StModel *mp = (StModel *)Memm_Alloc(1, 0x180, 0x10);
    int id = type ? 0x4b : 0x4a;
    Cdvd_GetSize2048(id);
    NL_ReadData(id, buff, -1, 0);
    cockpit_tmx = buff + *(int *)(buff + 0x20);
    char *tp2 = buff + *(int *)(buff + 0x30);
    SPRMNG_Init(&sprmng, buff + *(int *)(buff + 0x10));
    SPRMNG_OffsetTEX(&sprmng, plno << 10, plno << 5, 0x1500, 0x1900, 0x3d40, 0x3d60);
    ZeroMemory32b(mp, 0x60);
    DF_ReadTP2Pointer(tp2, mp, 0);
    CKPT_Create(&ckpt_mng, mp);
}
