/* src/event/talk2.cpp - radio chatter of friendly NPCs in missions. TCom (one StTalk2Com per friend, up to 6) holds the
 * 18 events (bits 1..18: player damaged / dead / leaving / in danger, mission start / success, friend in danger /
 * dead / doing well / complaining / damaged / following, ...) the friend can say (7 picked at random per sortie,
 * seq_code) and has not said yet (seq_code2). DF_TALK2_Init loads each friend's voice file and builds a trimmed SPU
 * bank with one random variant (of 3) per event (make_voice_bank); DF_TALK2_Exec scores the pending events of every
 * living friend (calc_value: the event functions, some of which count down the hit counters in value[]) and plays
 * the best one when it scores above 0.9 and no voice / message is running. The hit hooks (DF_TALK2_Damaged /
 * Destroyed) fill value[]. In Versus no friend is registered: only the hooks run, with empty loops. */
#include "acnb/sdk.h"
#include "acnb/ps2fp.h"

/* event value: count down value[n] (1 while it was > 0) */
static inline float talk2_take(StTalk2Com *tcom, int n)
{
    if (tcom->value[n] <= 0) return 0.0f;
    tcom->value[n]--;
    return 1.0f;
}

/* pads p with 0xff bytes to 16-byte alignment */
static inline char *talk2_pad(char *p)
{
    while ((uintptr_t)p & 0xf) *p++ = (char)0xff;
    return p;
}

/* Copies the entries k < max with tab[k] set of a sound bank (header hd, body bd) into buff: a 0x80-byte file header,
 * the bank header (hd's first 0x10 bytes), a 0x24-byte chunk directory, then the four header chunks (from hd + 0x30,
 * + 0x2c, + 0x28, + 0x24) cut down to the selected entries and renumbered 0..n-1, each padded with 0xff to 16 bytes,
 * then the selected ADPCM bodies. Returns the end. */
static char *make_voice_bank(void *buff, char *tab, int max, void *hd, void *bd)
{
    PS2_FRAME(0x540);
    PS2_STACK(int[0xd8], info, 0x1e0);   /* EmsVag vag info, 16 bytes per entry: [2] = body size */
    for (int k = 0; k < max; k++) EmsVag_GetVagInfo(hd, bd, k, &info[k * 4]);
    int n = 0;
    for (int k = 0; k < max; k++)
        if (tab[k]) n++;
    char *H = (char *)hd;
    int *c24 = (int *)(H + *(int *)(H + 0x24));
    int *c28 = (int *)(H + *(int *)(H + 0x28));
    int *c2c = (int *)(H + *(int *)(H + 0x2c));
    int *c30 = (int *)(H + *(int *)(H + 0x30));
    char *B = (char *)buff, *S = B + 0x80;
    memset(B, 0, 0x80);
    *(int *)(B + 8) = S - B;
    memcpy(S, hd, 0x10);
    memset(B + 0x90, 0, 0x24);
    *(int *)(B + 0x90) = *(int *)(H + 0x10);
    *(int *)(B + 0x94) = *(int *)(H + 0x14);
    char *q = talk2_pad(B + 0xb4);

    /* chunk from hd + 0x30: 8-byte entries, word 0 = offset of the entry's body in the new body */
    char *a = q;
    *(int *)(B + 0xb0) = a - S;
    ((int *)a)[0] = c30[0];
    ((int *)a)[1] = c30[1];
    ((int *)a)[3] = n - 1;
    q = a + 0x10 + n * 4;
    int *tbl = (int *)(a + 0x10), run = 0;
    for (int k = 0; k < max; k++) {
        if (!tab[k]) continue;
        *tbl++ = q - a;
        memcpy(q, (char *)c30 + c30[4 + k], 8);
        *(int *)q = run;
        q += 8;
        run += info[k * 4 + 2];
    }
    ((int *)a)[2] = q - a;
    q = talk2_pad(q);

    /* chunk from hd + 0x2c: 0x2a-byte entries, halfword 0 = new index */
    char *b = q;
    *(int *)(B + 0xac) = b - S;
    ((int *)b)[0] = c2c[0];
    ((int *)b)[1] = c2c[1];
    ((int *)b)[3] = n - 1;
    q = b + 0x10 + n * 4;
    tbl = (int *)(b + 0x10);
    int j = 0;
    for (int k = 0; k < max; k++) {
        if (!tab[k]) continue;
        *tbl++ = q - b;
        memcpy(q, (char *)c2c + c2c[4 + k], 0x2a);
        *(short *)q = (short)j;
        q += 0x2a;
        j++;
    }
    ((int *)b)[2] = q - b;
    q = talk2_pad(q);

    /* chunk from hd + 0x28: entries of byte[3] * 2 + 4 bytes, halfword 2 = new index; size word fixed */
    char *c = q;
    *(int *)(B + 0xa8) = c - S;
    ((int *)c)[0] = c28[0];
    ((int *)c)[1] = c28[1];
    ((int *)c)[2] = n * 8 + 0x10;
    ((int *)c)[3] = n - 1;
    q = c + 0x10 + n * 4;
    tbl = (int *)(c + 0x10);
    j = 0;
    for (int k = 0; k < max; k++) {
        if (!tab[k]) continue;
        *tbl++ = q - c;
        char *src = (char *)c28 + c28[4 + k];
        int len = (unsigned char)src[3] * 2 + 4;
        memcpy(q, src, len);
        *(short *)(q + 4) = (short)j;
        q += len;
        j++;
    }
    q = talk2_pad(q);
    *(int *)(B + 0xa4) = q - S;

    /* chunk from hd + 0x24: entries of byte[4] * 0x14 + 0x28 bytes, halfword 0x12 = new index */
    char *d = q;
    ((int *)d)[0] = c24[0];
    ((int *)d)[1] = c24[1];
    ((int *)d)[3] = n - 1;
    memset(d + 0x10, 0, n * 4);
    q = d + 0x10 + n * 4;
    tbl = (int *)(d + 0x10);
    j = 0;
    for (int k = 0; k < max; k++) {
        if (!tab[k]) continue;
        *tbl++ = q - d;
        char *src = (char *)c24 + c24[4 + k];
        int len = (unsigned char)src[4] * 0x14 + 0x28;
        memcpy(q, src, len);
        *(short *)(q + 0x24) = (short)j;
        q += len;
        j++;
    }
    ((int *)d)[2] = q - c;   /* (sic) measured from the previous chunk's start */
    q = talk2_pad(q);
    *(int *)(B + 0xc) = q - B;

    /* bodies: up to the block pair whose flag bytes end the sample (byte 1 == 7 with the loop bit of the block
     * before) */
    for (int k = 0; k < max; k++) {
        if (!tab[k]) continue;
        char *v = (char *)bd + *(int *)((char *)c30 + c30[4 + k]);
        int len = 0;
        while (!(v[len - 0xf] == 7 && (v[len - 0x1f] & 1) && len > 0)) len += 0x10;
        memcpy(q, v, len);
        q += len;
    }
    return q;
}

float calc_seq_value_frnd_fllow(StTalk2Com *tcom) { return talk2_take(tcom, 0x12); }

float calc_seq_value_frnd_support(StTalk2Com *tcom) { return talk2_take(tcom, 0x11); }

float calc_seq_value_frnd_roger(void) { return 0.0f; }

float calc_seq_value_frnd_damaged(StTalk2Com *tcom) { return talk2_take(tcom, 0xf); }

float calc_seq_value_frnd_leave(void) { return 0.0f; }

float calc_seq_value_frnd_complain(StTalk2Com *tcom) { return talk2_take(tcom, 0xd); }

float calc_seq_value_frnd_nobullet(void) { return 0.0f; }

float calc_seq_value_frnd_fineplay(StTalk2Com *tcom) { return tcom->value[0xb] < 5 ? 0.0f : 1.0f; }

/* AC friend: arg0p halfword 0x259c set; character: arg0p byte 8 bits 0xc0 */
float calc_seq_value_frnd_dead(StTalk2Com *tcom)
{
    StModel *m = tcom->mp;
    if (m->type == 4) return *(unsigned short *)((char *)m->arg0p + 0x259c) ? 1.0f : 0.0f;
    return *((unsigned char *)m->arg0p + 8) & 0xc0 ? 1.0f : 0.0f;
}

/* hp at or below 75 % (AC: hp / max hp; character: hp / its data's hp) */
float calc_seq_value_frnd_danger(StTalk2Com *tcom)
{
    StModel *m = tcom->mp;
    char *p = (char *)m->arg0p;
    float r;
    if (m->type == 4) {
        r = ps2_div((float)*(int *)(p + 4), (float)*(int *)(p + 0x7e4));
    } else {
        float full = (float)*(int *)(*(char **)(p + 0x5c) + 4);
        r = ps2_div((float)*(unsigned short *)(p + 0xc), full);
    }
    return r <= 0.75f ? 1.0f : 0.0f;
}

float calc_seq_value_miss_success(void) { return g_sortie.pwork->statMission == 3 ? 1.0f : 0.0f; }

float calc_seq_value_miss_start(void) { return (unsigned int)FRAME < 0x12d ? 1.0f : 0.0f; }

float calc_seq_value_plyr_fineplay(void) { return AcSubInfo[0].destroy_cnt < 10 ? 0.0f : 1.0f; }

float calc_seq_value_plyr_support(void) { return 0.0f; }

float calc_seq_value_plyr_danger(void) { return g_ac[0].stat & 8 ? 1.0f : 0.0f; }

float calc_seq_value_plyr_leave(void) { return g_ac[0].areastat == 2 ? 1.0f : 0.0f; }

float calc_seq_value_plyr_dead(void) { return g_ac[0].hp > 0 ? 0.0f : 1.0f; }

float calc_seq_value_plyr_damaged(StTalk2Com *tcom) { return talk2_take(tcom, 1); }

/* the event functions by event number (0x25bf00 in the original: entry 0 empty; called with (tcom, number)) */
static float (*const talk2_calc_tbl[19])(StTalk2Com *, int) = {
    0,
    [](StTalk2Com *t, int) { return calc_seq_value_plyr_damaged(t); },
    [](StTalk2Com *, int) { return calc_seq_value_plyr_dead(); },
    [](StTalk2Com *, int) { return calc_seq_value_plyr_leave(); },
    [](StTalk2Com *, int) { return calc_seq_value_plyr_danger(); },
    [](StTalk2Com *, int) { return calc_seq_value_plyr_support(); },
    [](StTalk2Com *, int) { return calc_seq_value_plyr_fineplay(); },
    [](StTalk2Com *, int) { return calc_seq_value_miss_start(); },
    [](StTalk2Com *, int) { return calc_seq_value_miss_success(); },
    [](StTalk2Com *t, int) { return calc_seq_value_frnd_danger(t); },
    [](StTalk2Com *t, int) { return calc_seq_value_frnd_dead(t); },
    [](StTalk2Com *t, int) { return calc_seq_value_frnd_fineplay(t); },
    [](StTalk2Com *, int) { return calc_seq_value_frnd_nobullet(); },
    [](StTalk2Com *t, int) { return calc_seq_value_frnd_complain(t); },
    [](StTalk2Com *, int) { return calc_seq_value_frnd_leave(); },
    [](StTalk2Com *t, int) { return calc_seq_value_frnd_damaged(t); },
    [](StTalk2Com *, int) { return calc_seq_value_frnd_roger(); },
    [](StTalk2Com *t, int) { return calc_seq_value_frnd_support(t); },
    [](StTalk2Com *t, int) { return calc_seq_value_frnd_fllow(t); },
};

/* best value of the friend's pending events (every pending event function runs: they count down); its number goes
 * to seq_no */
static float calc_value(StTalk2Com *tcom)
{
    PS2_FRAME(0x60);
    float best = 0.0f;
    int besti = 0;
    for (int i = 1; i < 0x13; i++) {
        if (!(tcom->seq_code2 & (1u << i))) continue;
        float v = talk2_calc_tbl[i] ? talk2_calc_tbl[i](tcom, i) : 0.0f;
        if (!(v <= best)) {
            best = v;
            besti = i;
        }
    }
    if (!besti) return 0.0f;
    tcom->seq_no = (short)besti;
    return best;
}

/* friend slot tid (1..5) gets its model */
void DF_TALK2_SetModel(int tid, StModel *mp)
{
    if (tid <= 0 || tid >= 6) return;
    StTalk2Com *t = &TCom[tid - 1];
    if (t->mp) return;
    t->mp = mp;
    mp->tcom = t;
}

int DF_TALK2_SearchTCom(int fid)
{
    for (int i = 0; i < TMng.num_tcom; i++)
        if (TCom[i].fid == fid) return TCom[i].on;
    return -1;
}

static StTalk2Com *talk2_find(StModel *m)
{
    StTalk2Com *t = TCom;
    for (int i = 0; i < TMng.num_tcom; i++, t++)
        if (t->on && t->mp == m) return t;
    return 0;
}

/* a friend destroyed something of another team: its "fine play" counter */
void DF_TALK2_Destroyed(StModel *mp, int s_type, int s_id)
{
    PS2_FRAME(0x20);
    StModel *sh = SPELL_Shooter2Model(s_type, s_id);
    if (!sh) return;
    StTalk2Com *t = talk2_find(sh);
    if (!t) return;
    if (!(sh->cflag & mp->cflag & 7)) t->value[0xb]++;
}

/* the player hit a friend (it complains) / a friend hit the player (its "damaged" counter) */
void DF_TALK2_Damaged(StModel *mp, int s_type, int s_id)
{
    PS2_FRAME(0x20);
    StModel *sh = SPELL_Shooter2Model(s_type, s_id);
    if (!sh) return;
    StTalk2Com *hit = talk2_find(mp), *shooter = talk2_find(sh);
    if (hit && sh == &MyAC[0]) hit->value[0xd] += 10;
    else if (shooter && mp == &MyAC[0]) shooter->value[0xf] += 10;
}

/* plays the best pending event of the living friends (> 0.9) when nothing is talking */
void DF_TALK2_Exec(void)
{
    PS2_FRAME(0x50);
    if (!TMng.execute) return;
    float best = 0.0f;
    StTalk2Com *sel = 0, *t = TCom;
    for (int i = 0; i < TMng.num_tcom; i++, t++) {
        if (!t->on) continue;
        StModel *m = t->mp;
        int dead = m->type == 4 ? ((unsigned char *)m->arg0p)[0x33] : m->type == 3 ? m->use & 4 : 0;
        if (dead) continue;
        float v = calc_value(t);
        if (!(v <= best)) {
            best = v;
            sel = t;
        }
    }
    if (TMng.state > 0 && EmsVag_VagCheckEos()) TMng.state = 0;
    if (TMng.state || MsgWnd_IsBusy(1) || best <= 0.9f) return;
    int idx = -1, k = 0;
    for (int e = 1; e < 0x14; e++) {
        if (!(sel->seq_code & (1u << e))) continue;
        if (e == sel->seq_no) {
            idx = k;
            break;
        }
        k++;
    }
    Vags_Play(idx, 2, TVoice[sel->on - 1]);
    TMng.state = sel->on;
    sel->seq_code2 &= ~(1u << sel->seq_no);
}

/* voice bank of friend slot tid: a 0x70000-byte piece of the voice buffer, one random variant (of 3) per event the
 * friend can say */
void DF_TALK2_ReadSec2(int tid, int fid)
{
    PS2_FRAME(0xa0);
    Cdvd_GetSize2048(fid + 0xa96);
    Memm_GetRest();
    char *p = 0;
    if (voice_buff_top && voice_buff_ptr) {
        p = (char *)(((uintptr_t)voice_buff_ptr + 0xf) & ~(uintptr_t)0xf);
        if (voice_buff_size - (p - voice_buff_top) < 0x70000) p = 0;
        else voice_buff_ptr = p + 0x70000;
    }
    TVoice[tid - 1] = p;
    char *buf = (char *)Memm_GetNext();
    NL_ReadData(fid + 0xa96, buf, -1, 0);
    StTalk2Com *t = &TCom[tid - 1];
    PS2_STACK(char[0x36], tab, 0x60);
    memset(tab, 0, 0x36);
    for (int e = 0; e < 0x12; e++)
        if (t->seq_code & (1u << (e + 1))) tab[e * 3 + ps2_mod32(ps2_rand(), 3)] = 1;
    char *end = make_voice_bank(g_pbufRead, tab, 0x36, buf + *(int *)(buf + 8), buf + *(int *)(buf + 0xc));
    memcpy(TVoice[tid - 1], g_pbufRead, end - g_pbufRead);
}

void DF_TALK2_Start(void) { TMng.execute = 1; }

/* one slot per friend that is not forced (7 random events of 18), voice banks from buffp */
void DF_TALK2_Init(void *buffp, int buff_size)
{
    PS2_FRAME(0x90);
    memset(TCom, 0, 0x300);
    memset(TVoice, 0, 0x18);
    memset(&TMng, 0, 0x44);
    voice_buff_size = buff_size;
    voice_buff_top = (char *)buffp;
    voice_buff_ptr = (char *)buffp;
    int n = DF_NPCTAB_GetFrndNum();
    for (int i = 0; i < n; i++) {
        if (DF_NPCTAB_IsForce(i)) continue;
        StTalk2Com *t = 0;
        if (TMng.num_tcom < 6) {
            t = &TCom[TMng.num_tcom++];
            if (t->on) t = 0;
            else t->on = (unsigned short)TMng.num_tcom;
        }
        if (!t) continue;   /* (sic) the original writes through the NULL slot (unreachable: TCom is cleared above) */
        t->fid = (unsigned short)DF_NPCTAB_GetFrndID(i);
        PS2_STACK(int[18], perm, 0x40);
        randperm(perm, 0x12);
        unsigned int code = 0;
        for (int k = 0; k < 7; k++) code |= 1u << perm[k];
        t->seq_code = code;
        t->seq_code2 = t->seq_code;
        DF_TALK2_ReadSec2(t->on, t->fid);
    }
}
