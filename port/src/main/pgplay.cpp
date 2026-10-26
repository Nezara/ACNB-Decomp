/* src/main/pgplay.cpp - "PGB" polygon-movie player (in-game cut-scenes driven by the PolyG library; g_PgpMng via
 * polyg.cpp). Native so far: PGP_Proc (per frame; in Versus nothing starts a movie, so it returns at is_play == 0)
 * PGP_EnableDeleteSFX and the stage-load pair PGP_Create / PGP_RegistPGBPack (via PolygRegist). The cut-scene
 * internals (__pgplay_*, PGP_Start / SetPGBFile) only run in single-player and stay interpreted until that pass. */
#include "acnb/port.h"
#include "acnb/local/src/main/pgplay.h"
#include "acnb/ps2fp.h"
#include "acnb/nodwarf.h"

#ifdef ACNB_SOURCEPORT
char pgplay_polyg_name[5] = "test";
#endif

/* Returns 1 while the movie still plays, 0 when idle or at its end. */
int PGP_Proc(struct_95bd2017 *mng)
{
    PS2_FRAME(0x60);
    if (!mng->is_play) return 0;
    if (g_sortie.pconstant->modeSortie == 0) {
        g_ac[0].rb_stat = 0;
        AcWep_ReleaseWeapon(&g_ac[0], g_ac[0].equip_weapon);
    }
    __pgplay_PgpPolygProc(mng);
    if (mng->is_end) {
        char loop = mng->is_loop;
        if (mng->org_camid != -1) {
            ViewCamID[0] = mng->org_camid;
            CAM[ViewCamID[0]].groupidbk = 0xff;
            mng->org_camid = -1;
        }
        mng->is_play = 0;
        mng->is_loop = 0;
        __pgplay_CheckOutModel(mng);
        for (int i = 0; i < NowPlayerNum; i++) AcCtrl_NotifyControlPolyG(&g_ac[i], 0);
        g_sortie.pwork->isPlayPolyg = 0;
        if (loop) {
            fsPs2PolyG_InitUseOB(&mng->polyg, mng->pgb_top, mng->pgb_size);
            __pgplay_CheckModelList(mng);
            __pgplay_CheckOutModel(mng);
            memset(s_PGACFlag, 0, 0x10);
            memset(s_PGACWepFlag, 0, 0x10);
            for (int i = 0; i < NowPlayerNum; i++) AcCtrl_NotifyControlPolyG(&g_ac[i], 0);
            __pgplay_CheckInModel(mng);
            mng->frame = 0;
            mng->step_time = 16.666668f;   /* 0x41855556 */
            mng->is_first = 1;
            mng->is_end = 0;
            mng->is_play = 0;
            mng->is_loop = 0;
            if (mng->org_camid == -1) {
                mng->org_camid = (char)ViewCamID[0];
                ViewCamID[0] = 5;
                CAM[ViewCamID[0]].groupidbk = 0xff;
            }
            mng->is_play = 1;
            mng->is_loop = 1;
            if (s_bDeleteSFX) {
                SPELL2_DeleteFire();
                MAGIC2_DeleteAllMagic();
                Mismoke_AllDelete();
            }
            g_sortie.pwork->isPlayPolyg = 1;
        }
        AcWep_EquipWeapon(&g_ac[0], g_ac[0].equip_weapon);
    }
    for (int i = 0; i < NowPlayerNum; i++) AcCtrl_UpdateMatrixForPolyG(&g_ac[i]);
    return mng->is_end == 0;
}

void PGP_EnableDeleteSFX(int enableFlag)
{
    s_bDeleteSFX = enableFlag;
}

/* stores the pack (NULL allowed); returns the end of the pack or NULL */
void *PGP_RegistPGBPack(struct_95bd2017 *mng, PackPGBHeadSt *headp)
{
    mng->pack_head = headp;
    if (!headp) return 0;
    return (char *)headp + headp->size;
}

/* idle state, the PolyG evaluator (name "test", 60 fps step 16.666668 ms) and its 1 MB work buffer */
void PGP_Create(struct_95bd2017 *mng)
{
    PS2_FRAME(0x30);
    mng->frame = 0;
    mng->step_time = ps2_u2f(0x41855556);
    mng->is_first = 1;
    mng->is_end = 0;
    mng->is_play = 0;
    mng->is_loop = 0;
    fsPs2PolyG_Init(&mng->polyg);
    fsPs2PolyG_Create(&mng->polyg, pgplay_polyg_name, ps2_u2f(0x41855556));
    void *buf = Memm_Alloc(1, 0x100000, 0x80);
    fsPs2PolyG_AllocBuffer(&mng->polyg, buf, 0x100000);
    mng->org_camid = -1;
    mng->pack_head = 0;
    mng->pgb_top = 0;
    mng->pgb_size = 0;
    mng->nocam = 0;
}
