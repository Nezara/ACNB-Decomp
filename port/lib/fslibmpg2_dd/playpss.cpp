/* lib/fslibmpg2_dd/playpss.cpp - the PSS player's buffer sizing (only mpg_GetRequiredBufferSize is native; the player
 * itself is phase 3 step 6). mpg_GetRequiredBufferSize prints the static buffer layout of the video mode (2 or 3) and
 * returns the work buffer size the caller must hand the player: 0x4dd9c0 (mode 2) / 0x5af9c0 (mode 3), + 0x24000 with
 * digital audio; any other mode prints nothing and returns 0. The printed parts do not add up to the result (as the
 * original). */
#include "acnb/port.h"
#include "acnb/sdk.h"
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
char mpg_msg_rule[55] = "#####################################################\n";
char mpg_msg_static[23] = "\tStatic buffer size :\n";
char mpg_msg_voBufData[25] = "\t\tvoBufData        : %d\n";
char mpg_msg_voBufTag[25] = "\t\tvoBufTag         : %d\n";
char mpg_msg_viBufTag[25] = "\t\tviBufTag         : %d\n";
char mpg_msg_mpegWork[25] = "\t\tmpegWork         : %d\n";
char mpg_msg_defStack[25] = "\t\tdefStack         : %d\n";
char mpg_msg_readBuf[25] = "\t\treadBuf          : %d\n";
char mpg_msg_audioBuff[25] = "\t\taudioBuff        : %d\n";
char mpg_msg_digitalAudioBuff[25] = "\t\tdigitalAudioBuff : %d\n";
char mpg_msg_viBufData[25] = "\t\tviBufData        : %d\n";
char mpg_msg_videoDecStatc[25] = "\t\tvideoDecStatc    : %d\n";
char mpg_msg_timeStamp[25] = "\t\ttimeStamp        : %d\n";
#endif

/* the 11 report lines between two rules (the parts that differ by mode: voBufData, voBufTag, mpegWork) */
static void mpg_report(int voBufData, int voBufTag, int mpegWork)
{
    ps2_printf(mpg_msg_rule);
    ps2_printf(mpg_msg_static);
    ps2_printf_d(mpg_msg_voBufData, voBufData);
    ps2_printf_d(mpg_msg_voBufTag, voBufTag);
    ps2_printf_d(mpg_msg_viBufTag, 0x1040);
    ps2_printf_d(mpg_msg_mpegWork, mpegWork);
    ps2_printf_d(mpg_msg_defStack, 0x2000);
    ps2_printf_d(mpg_msg_readBuf, 0x70040);
    ps2_printf_d(mpg_msg_audioBuff, 0xc000);
    ps2_printf_d(mpg_msg_digitalAudioBuff, 0xc000);
    ps2_printf_d(mpg_msg_viBufData, 0x80000);
    ps2_printf_d(mpg_msg_videoDecStatc, 0x4000);
    ps2_printf_d(mpg_msg_timeStamp, 0x3000);
    ps2_printf(mpg_msg_rule);
}

int mpg_GetRequiredBufferSize(unsigned short video, unsigned short digital)
{
    PS2_FRAME(0x40);
    int size = 0;
    if (video == 2) {
        mpg_report(0x230000, 0x6ac80, 0x13c780);
        size = 0x4dd9c0;
        if (digital) size += 0x24000;
    } else if (video == 3) {
        mpg_report(0x2a8000, 0x81480, 0x17ff80);
        size = 0x5af9c0;
        if (digital) size += 0x24000;
    }
    return size;
}
