/* src/main/main.cpp - the program entry (main ELF). main() is a byte state machine (s_stepMainCur / Next / Prev):
 * 0 boots the PS2 side (__InitPs2: IOP, DMAC / VIF / VU / GS reset, IRX modules, draw / sound / pad, file system,
 * root files) and starts the ThreTask worker threads, 1 initialises the game (__InitApp: memory, system data,
 * textures, emblems, missions, assembly tables), 2 loads OLMENU and enters the menu, 3 runs the menu
 * (DF_MENU_MainLoop) and picks the next step from MyI (ending movie 10, prologue movie 9, else a sortie), 4 sets
 * the sortie up from g_nextstage (__DoStepDbgMapSelect), 5 loads OLSORTIE and runs the sortie (__DoStepSortie, next
 * step from MyI.mainproc), 6 handles an i.LINK error, 7 / 8 exit / restart. __LoadOverlay(0 / 1) loads
 * OLSORTIE.BIN / OLMENU.BIN to the overlay address.
 *
 * Hybrid: main is the bottom native frame of the whole game (crt0 -> main); it never returns in a normal run.
 * Original quirks kept: __DoStepSortie's mainproc 7 arm returns whatever v0 LoopSortie_End left (stale register,
 * PS2_STALE_GPR; 0 in the source port); main state 0 clears FRAME before init_timer0 (delay slot); Game_ResultArena /
 * ExArena / AddExArenaSortieCount and DF_GAME_SystemDataRead get register arguments the (empty / void) callees ignore.
 * versus.quick (runtime/quick_match.c) sets the first Versus sortie's random seed through hook_step_map_select. */
#include "acnb/hooks.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include <string.h>

#ifdef ACNB_SOURCEPORT
unsigned char s_lifeContinue = 1;
int s_idMainThread = -1;
char main_movie_e1[29] = "cdrom0:\\MOVIE\\DISCN\\E1.PSS;1";
char main_movie_e2[29] = "cdrom0:\\MOVIE\\DISCN\\E2.PSS;1";
char main_movie_i1[29] = "cdrom0:\\MOVIE\\DISCN\\I1.PSS;1";
char main_movie_i2[29] = "cdrom0:\\MOVIE\\DISCN\\I2.PSS;1";
char main_ovl_sortie[23] = "cdrom0:\\OLSORTIE.BIN;1";
char main_ovl_menu[21] = "cdrom0:\\OLMENU.BIN;1";
/* the linker's overlay group table (0x27ac00, 0x80 bytes): [0] main ELF load address, [1] the overlay area (read by
 * __LoadOverlay, handed to Overlay_Load, unused there in the source port). globals.h has it as [0] (no DWARF size), so
 * the definition takes its symbol by name */
void *main_overlay_group_addresses_[32] __asm__("__overlay_group_addresses") = {(void *)0x100000, (void *)0x1df5f00};
#endif

#define MAIN_MOVIE_BUF_SIZE 0x160a000
#define MAIN_MOVIE_SKIP_PAD 0x4008

static inline unsigned char main_mode_sortie(void) { return g_sortie.pconstant->modeSortie; }

/* Movie_PlayEasy(Env_ConvertPath(path), g_pbufGlobal); path is main ELF rodata (interpreted callees read it) */
static inline void main_play_movie(char *path)
{
    Movie_PlayEasy(Env_ConvertPath(path), g_pbufGlobal);
}

int main(void)
{
    PS2_FRAME(0x30);
    PS2_STACK(struct struct_6f8e3b2c, info, 0x20);
    if (!s_lifeContinue) return 0;
    do {
        if (s_stepMainCur != s_stepMainNext) {
            s_stepMainPrev = s_stepMainCur;
            s_stepMainCur = s_stepMainNext;
        }
        switch (s_stepMainCur) {
        case 0:
            s_idMainThread = ps2_GetThreadId();
            ps2_ChangeThreadPriority(s_idMainThread, 0x40);
            ps2GetSystemInfo(&info);
            FullAllocAndFree();
            FRAME = 0;
            init_timer0();
            __InitPs2();
            ThreTask_Init(2);
            s_stepMainNext = 1;
            break;
        case 1:
            __InitApp();
            s_stepMainNext = 2;
            break;
        case 2:
            DF_LOOP_BlackMask();
            g_gamedata[0].option.mastervol = 0x7f;
            DF_SND_SetMVolume(0x3fff);
            Game_InitAllData();
            DF_ACFONT_Init();
            MyI.myplayerno[0] = 0;
            __LoadOverlay(1);
            DF_MENU_Init();
            s_stepMainNext = 3;
            break;
        case 3: {
            unsigned char next;
            DF_MENU_MainLoop(g_pbufGlobal, MAIN_MOVIE_BUF_SIZE, 1);
            if (MyI.ending) next = 10;
            else if (MyI.prologue) next = 9;
            else {
                Draw_Init();
                unsigned char mode = main_mode_sortie();
                if (mode == 4 || mode == 5) MyI.random_seed = (unsigned char)play_info.rand_seed;
                else MyI.random_seed = 100;
                next = 4;
            }
            s_stepMainNext = next;
            break;
        }
        case 4:
            s_stepMainNext = __DoStepDbgMapSelect();
            break;
        case 5:
            s_stepMainNext = __DoStepSortie();
            break;
        case 6: {
            int mes = 2;
            if (MyI.ilinkerr == 2) mes = 4;
            LOOP_LinkError(mes);
            MyI.mainproc = 0;
            terminate_link();
            __LoadOverlay(1);
            DF_MENU_Init();
            DF_MENU_SetResume(2);
            s_stepMainNext = 3;
            break;
        }
        case 7:
            s_lifeContinue = 0;
            s_idMainThread = -1;
            s_stepMainNext = 1;
            break;
        case 8:
            s_stepMainNext = 1;
            break;
        case 9:
            Draw_SetupFrameBuffer(1, 0);
            MMO_Play(0x32, -1, g_pbufGlobal, MAIN_MOVIE_BUF_SIZE, MAIN_MOVIE_SKIP_PAD);
            MyI.prologue = 0;
            __LoadOverlay(1);
            s_stepMainNext = 3;
            break;
        case 10: {
            int first = Game_GetClearCount(0) == 0;
            Draw_SetupFrameBuffer(1, 0);
            MMO_Play(0x34, -1, g_pbufGlobal, MAIN_MOVIE_BUF_SIZE, MAIN_MOVIE_SKIP_PAD);
            MMO_Play(0x33, 8, g_pbufGlobal, MAIN_MOVIE_BUF_SIZE, MAIN_MOVIE_SKIP_PAD);
            Game_IncClearCount(0);
            MyI.ending = 0;
            __LoadOverlay(1);
            if (first) DF_MENU_SetResume(1);
            s_stepMainNext = 3;
            break;
        }
        default:
            break;
        }
    } while (s_lifeContinue);
    return 0;
}

static unsigned char __DoStepSortie(void)
{
    PS2_FRAME(0x10);
    clear_timer0();
    MsgWnd_Clear();
    __LoadOverlay(0);
    int r = LoopSortie_Begin();
    if (r < 0) {
        if (r == -3) {
            unsigned char mode = main_mode_sortie();
            if (mode == 6 || mode == 5 || mode == 4) {
                __LoadOverlay(1);
                DF_MENU_Init();
                DF_MENU_SetResume(2);
            }
            return 3;
        }
        LoopSortie_End();
        return 2;
    }
    LoopSortie_Do();
    LoopSortie_End();
    unsigned char mp = MyI.mainproc;
    MyI.mainproc = 0;
    switch (mp) {
    case 1:
        __ChangeStage();
        return 5;
    case 2:
        __LoadOverlay(1);
        return 3;
    case 3: {
        unsigned char mode = main_mode_sortie();
        if (mode == 3) {
            Game_ResultExArena();   /* (0, MyI.rankerid, MyI.vswinner == 0): empty native, arguments unused */
        } else if (mode == 5 || mode == 4) {
            if (MyI.ilinkerr != 0) return 6;
        } else if (mode == 2) {
            Game_ResultArena();     /* same arguments as above */
        }
        unsigned char vsnext = MyI.vsnext;
        if (vsnext == 2) {
            MyI.replaymode = 1;
            return 5;
        }
        if (vsnext == 1) {
            MyI.replaymode = 0;
            mode = main_mode_sortie();
            if (mode == 4 || mode == 5) {
                if (MyI.random_seed < 0xff) MyI.random_seed = MyI.random_seed + 1;
                else MyI.random_seed = 2;
            }
            return 4;
        }
        __LoadOverlay(1);
        return 3;
    }
    case 4:
        if (g_nextmenu.misres.idGameOver == 2) main_play_movie(main_movie_e2);
        else if (g_nextmenu.misres.idGameOver == 1) main_play_movie(main_movie_e1);
        __LoadOverlay(1);
        return 3;
    case 7:
        /* the original returns v0 untouched: what LoopSortie_End left in it */
        return (unsigned char)PS2_STALE_GPR(2);
    case 8:
        return 2;
    case 9:
    case 10:
        if (mp == 10) main_play_movie(main_movie_i2);
        else if (mp == 9) main_play_movie(main_movie_i1);
        __LoadOverlay(1);
        return 3;
    default:   /* 0, 5, 6, >= 11 */
        return 4;
    }
}

static unsigned char __DoStepDbgMapSelect(void)
{
    PS2_FRAME(0x10);
    StgInfo.cont_cnt = 0;
    AcOpt.low = 0;
    if (main_mode_sortie() != 2) MyI.rec_ai = 0;
    NL_SetMode(0);
    NL_LoadTexture();
    MyI.replaymode = 0;
    if (g_nextstage.sortie_mode == 0) NL_SetMode(1);
    switch (g_nextstage.sortie_mode) {
    case 2:
    case 6:
        Game_SetKeyDefault(1);
        break;
    case 3:
        Game_SetKeyDefault(1);
        Game_SetKeyDefault(2);
        Game_SetKeyDefault(3);
        Game_AddExArenaSortieCount();   /* original a0 = 0: empty native */
        break;
    case 5:
        MyI.replaymode = play_info.f_replay != 0;
        break;
    default:   /* 0, 1, 4, > 6 */
        break;
    }
    MyI.VSM_success[1] = 0;
    MyI.VSM_success[0] = 0;
    MyI.vswinner = 0xff;
    MyI.pikwep_en = 0;
    DF_EVENT_EvStageWkInit();
    if (hook_step_map_select) hook_step_map_select();
    return 5;
}

static void __SearchRootFiles(void)
{
    PS2_FRAME(0x810);
    PS2_STACK(char[2048], buf, 0x10);   /* interpreted File_Read fills it */
    Cdvd_SetupFile();
    MultiStm_SetupFile();
    File_Read(0x186a0, 0x800, buf, 1);
    Ac_SetDiscType(buf[0x148]);
}

static int __LoadOverlay(int oid)
{
    PS2_FRAME(0x120);
    PS2_STACK(char[256], name, 0x20);   /* handed to Overlay_Load */
    void *addr = _overlay_group_addresses[1];
    memset(name, 0, sizeof name);
    if (oid < 0 || oid >= 2) return 0;
    if (oid == 1) strcpy(name, main_ovl_menu);
    else if (oid == 0) strcpy(name, main_ovl_sortie);
    return Overlay_Load(name, addr) != 0;
}

static void __InitApp(void)
{
    PS2_FRAME(0x10);
    MyI.random_seed = 100;
    Memm_InitGeneralBuffer();
    DF_GAME_SystemDataRead();   /* original a0 = 0, unused */
    DF_SND_ReadSmartSSTable();
    NL_LoadTexture();
    ScrEff_Init();
    MsgWnd_Init();
    Ads_Init();
    Ac2_Mcf_Init();
    DF_AMAP_ReadData();
    DF_EMB_InitCLUT();
    DF_EMB_Init(EmblemTM2[0], EmblemSTM2[0]);
    DF_EMB_Init(EmblemTM2[1], EmblemSTM2[1]);
    Miss_Init();
    StageInfo_Init();
    DestObjMem_Init();
    AsmParam_Load();
    AcAsm_LoadLoadageRatioTable();
}

static void __InitPs2(void)
{
    PS2_FRAME(0x30);
    PS2_STACK(int, irx_result, 0x2c);   /* out parameter of the interpreted _LoadEmbeddedIrx */
    Iop_Init(0x200);
    sceDmaReset(1);
    sceDevVif0Reset();
    sceDevVu0Reset();
    sceDevVif1Reset();
    sceDevVu1Reset();
    sceSifInitIopHeap();
    sceGsResetPath();
    sceGsResetGraph(0, 1, 2, 1);
    long long r = _LoadEmbeddedIrx(&irx_result);
    Draw_Init();
    DF_LOOP_BlackMask();
    Snd_Init();
    Pad_Init();
    sceGsSyncV(0);
    DF_LOOP_PadWait();
    if (r >= 0 && irx_result != 0) ps2_exit(0);
    File_Init();
    __SearchRootFiles();
    installer_Init();
}
