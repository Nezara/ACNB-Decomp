/* src/collision/hitchr.cpp - collision against characters (whole file): the ACs (MyAC[0..NowPlayerNum)) and the
 * stage characters (Chr[0..StgInfo.cnum)), each a Y-cylinder at loc (tall, R). Results go through the scratchpad
 * work area (hitwk.h). */
#include "acnb/sdk.h"
#include "hitwk.h"

/* character cylinder test of the work cylinder against every other character: IntVec = average push-out of up to 8
 * hits (s_AddVec), y = lowest + highest hit height */
int HitChrHitProcChr(StModel *pChrMdl, float *ChrCyP, float ChrCyH, float ChrCyR, float *IntVec)
{
    PS2_FRAME(0x90);
    PS2_STACK(float[4], loc, 0x80);
    HitSprWk *w = HIT_WK;
    int hit = 0;
    s_NowAddNum = 0;
    memcpy(w->cyl.P, ChrCyP, 16);
    w->cyl.H = ChrCyH;
    w->cyl.R = ChrCyR;
    auto test = [&](StModel *m) {
        memcpy(loc, m->loc, 16);
        HitFuncCylinderCylinderSPR(loc, m->tall, m->R);
        if (!w->info.hit) return;
        hit = 1;
        if (s_NowAddNum < 8) {
            memcpy(s_AddVec[s_NowAddNum++], w->info.hitpos, 16);
            HitFuncSetHitMdl(m);
        }
    };
    int n = NowPlayerNum;
    StModel *m = MyAC;
    for (int i = 0; i < n; i++, m++)
        if (!(m->use & 0x42) && pChrMdl != m) test(m);
    int cnum = StgInfo.cnum;
    m = Chr;
    for (int i = 0; i < cnum; i++, m++)
        if (!(m->use & 0x42) && !(m->array->mflag & 0x40000) && pChrMdl != m) test(m);
    if (!hit) return 0;
    IntVec[0] = 0.0f;
    IntVec[1] = 0.0f;
    IntVec[2] = 0.0f;
    IntVec[3] = 1.0f;
    float ymax = 0.0f, ymin = 0.0f;
    int num = s_NowAddNum;
    for (int i = 0; i < num; i++) {
        for (int k = 0; k < 3; k++) IntVec[k] = IntVec[k] + s_AddVec[i][k];
        float y = s_AddVec[i][1];
        if (!(y <= ymax)) ymax = y;
        if (y < ymin) ymin = y;
    }
    float inv = 1.0f / (float)num;
    for (int k = 0; k < 3; k++) IntVec[k] = IntVec[k] * inv;
    IntVec[1] = ymin + ymax;
    return 1;
}

/* the line P + t N L against every character (skipping removeType characters with id removeID); each hit
 * shortens the line, so the nearest hit wins: hitpos = P + V tt1 */
int HitChrHitProcLine(float *LP, float *LN, float LL, int removeType, int removeID)
{
    PS2_FRAME(0xa0);
    PS2_STACK(float[4], loc, 0x90);
    HitSprWk *w = HIT_WK;
    int hit = 0;
    memcpy(w->line.P, LP, 16);
    memcpy(w->line.N, LN, 16);
    w->line.L = LL;
    auto test = [&](StModel *m) {
        memcpy(loc, m->loc, 16);
        HitFuncLineCylinderSPR(loc, m->tall, m->R);
        if (!w->info.hit) return;
        float t = w->info.tt1;
        hit = 1;
        for (int k = 0; k < 3; k++) w->line.V[k] = w->line.V[k] * t;
        w->line.L = w->line.L * t;
        HitFuncSetHitMdl(m);
    };
    int n = NowPlayerNum;
    StModel *m = MyAC;
    for (int i = 0; i < n; i++, m++)
        if (!(m->use & 2) && (!(m->type & removeType) || m->aid != removeID)) test(m);
    int cnum = StgInfo.cnum;
    m = Chr;
    for (int i = 0; i < cnum; i++, m++)
        if (!(m->use & 2) && !(m->array->mflag & 0x40000) && (!(m->type & removeType) || m->aid != removeID)) test(m);
    if (hit) {
        float t = w->info.tt1, v[3];
        for (int k = 0; k < 3; k++) v[k] = w->line.V[k] * t;
        for (int k = 0; k < 3; k++) w->info.hitpos[k] = w->line.P[k] + v[k];
        w->info.hitpos[3] = w->line.P[3];
    }
    return hit;
}

/* does the segment LP..LE hit any character (cflag & removeFlag skips; stage characters with id removeID too) */
int HitChrIsHitLineChr(float *LP, float *LE, int removeID, int removeFlag)
{
    PS2_FRAME(0x80);
    PS2_STACK(float[4], loc, 0x70);
    HitSprWk *w = HIT_WK;
    memcpy(w->line.P, LP, 16);
    for (int k = 0; k < 3; k++) w->line.V[k] = LE[k] - LP[k];
    w->line.V[3] = LE[3];
    const float *V = w->line.V;
    float x = V[0] * V[0], y = V[1] * V[1], z = V[2] * V[2];
    w->line.L = ps2_sqrt(z + (x + y));
    float q = vu_rcp_len3(V);
    for (int k = 0; k < 3; k++) w->line.N[k] = V[k] * q;
    w->line.N[3] = 0.0f;
    int n = NowPlayerNum;
    StModel *m = MyAC;
    for (int i = 0; i < n; i++, m++) {
        if ((m->use & 2) || (m->cflag & removeFlag)) continue;
        memcpy(loc, m->loc, 16);
        HitFuncLineCylinderCheckSPR(loc, m->tall, m->R);
        if (w->info.hit) return 1;
    }
    int cnum = StgInfo.cnum;
    m = Chr;
    for (int i = 0; i < cnum; i++, m++) {
        if ((m->use & 2) || (m->array->mflag & 0x40000) || (m->cflag & removeFlag) || m->aid == removeID) continue;
        memcpy(loc, m->loc, 16);
        HitFuncLineCylinderSPR(loc, m->tall, m->R);
        if (w->info.hit) return 1;
    }
    return 0;
}
