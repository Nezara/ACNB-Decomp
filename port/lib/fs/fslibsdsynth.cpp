/* lib/fs/fslibsdsynth.cpp - EE half of FSSDSYTH (IOP module 4): sound effects as a byte stream, reverb table, voice
 * info poll, SPU2 / bank / SQ management (G2 = the SE side, 0x147970 .. 0x148ae0; G3 = SQ / bank / stereo, 0x148ae0 ..
 * 0x149b70; G4 = SystemStart / SifTransSync / SifTransBuffer / Quit / Init / _Cosf / _Sinf, 0x149b70 .. 0x14a640).
 * SQ / bank side: IOP heap blocks (sceSifAllocIopHeap) hold the HD / SQ copies; their addresses go into later RPC
 * words, so the alloc / free order is the original's (AddBank: HD, mono HD, then the halving BD chunk buffer).
 * Further quirks kept (G3): StopSq / CheckSqEos / PlaySq accept index 4 (sq_tbl[4] = se_tbl[0..3]); RemoveSq has no
 * range check; AddBankFromEE2 accepts port 16 (s_iPortSpuMap[16] = the word after the map), its SPU destination
 * advances by the unrounded chunk and b->ch is never assigned; SetStereo / SetMono advance their bank / SQ pointer
 * only after a re-registration, so an empty slot freezes it for the remaining iterations.
 * SE stream: records are appended to s_ucSeBuf (word 0 = used length, bytes from +4) through s_esmData.se_strp and
 * DMA'd to the IOP once per frame by fssdsynthSeFlush (size (len + 4 + 15) & ~15: up to 15 bytes of older residue go
 * along, so the stream is written in place into the one guest buffer, never staged). Record = `ff <hsyn port> n
 * payload[n]`; events: `c0+ch prog` (program change), `f9 kind ch v0 v1` (SeOn: pan / bend / volume), `fd kind ch key id
 * v0 v1` (ChangeSeParam volume / pan / bend, note on / off kind 0x10), `fe 01 id32 st prog note velo pan 00 00`
 * (stream voices). The original writes through the uncached alias se_strp | 0x20000000; the native writes the same
 * RAM through HOSTPTR(se_strp) (the alias only bypasses the data cache).
 * RPC words a wrapper does not name are register / stack residue in the original; the natives pass 0 (blanked by
 * strace -NormScrc).
 * Quirks kept: SetReverb / RemoveReverb accept index 4 (= cur_reverb itself); SeOn / SeStreamOn return se_idx; SeFlush
 * returns -1 without clearing len when sceSifSetDma fails; ChangeSeParam with an unknown type writes only `ff bk 07`
 * and still adds 10 (7 stale bytes); SetSurroundVolume encodes a negative volume as 0x8000 - |x|; PackSeStream-
 * SurroundOn advances its pack pointer for skipped entries too. */
#include "acnb/port.h"
#include "acnb/nodwarf.h"
#include "acnb/sdk.h"
#include "acnb/local/lib/fs/fslibsdsynth.h"

#ifdef ACNB_SOURCEPORT
char fssynth_msg_se_idx[46] = "<fslibsdsynth> se index isn't exist. - id=%d\n";
char fssynth_msg_se_room[45] = "<fslibsdsynth> can't se on. - valid-size=%d\n";
char fssynth_msg_se_id[46] = "<fslibsdsynth> can't se on. - wrong-se-id=%d\n";
char fssynth_msg_setrev_id[47] = "<fssdsynthSetReverb> wrong reverb id. - id=%d\n";
char fssynth_msg_rmrev_id[50] = "<fssdsynthRemoveReverb> wrong reverb id. - id=%d\n";
char fssynth_msg_rmrev_undef[53] = "<fssdsynthRemoveReverb> reverb not defined. - id=%d\n";
char fssynth_msg_rev_full[40] = "<fslibsdsynth> reverb table over flow.\n";
char fssynth_msg_stopsq_idx[40] = "<fssdsynthStopSq> wrong index. - id=%d\n";
char fssynth_msg_stopsq_undef[42] = "<fssdsynthStopSq> sq not define. - id=%d\n";
char fssynth_msg_stopsq_fail[50] = "<fssdsynthStopSq> can't stop sq. - id=%d, res=%d\n";
char fssynth_msg_eos_idx[44] = "<fssdsynthCheckSqEos> wrong index. - id=%d\n";
char fssynth_msg_playsq_idx[40] = "<fssdsynthPlaySq> wrong index. - id=%d\n";
char fssynth_msg_playsq_undef[42] = "<fssdsynthPlaySq> sq not define. - id=%d\n";
char fssynth_msg_playsq_fail[51] = "<fssdsynthPlaySq> can't Start sq. - id=%d, res=%d\n";
char fssynth_msg_rmsq_iop[42] = "<fssdsynthRemoveSq> error iop addr. - %x\n";
char fssynth_msg_rmsq_undef[49] = "<fssdsynthRemoveSq> sq file is't exist. - id=%d\n";
char fssynth_msg_addr[9] = "addr=%p\n";
char fssynth_msg_bank[9] = "bank=%d\n";
char fssynth_msg_addsq_full[44] = "<fssdsynthAddSqFromEE> sq table over flow.\n";
char fssynth_msg_addsq_port[53] = "<fssdsynthAddSqFromEE> wrong port number. - port=%d\n";
char fssynth_msg_addsq_alloc[45] = "<fssdsynthAddSqFromEE> can't alloc iop heap\n";
char fssynth_msg_addsq_iop[25] = "sq-iop-addr=0x%x...0x%x\n";
char fssynth_msg_addsq_hd[60] = "<fssdsynthAddSqFromEE> can't load hd to hsyn csl. - res=%d\n";
char fssynth_msg_addsq_bank[32] = "<fssdsynthAddSqFromEE> bank=%d\n";
char fssynth_msg_addsq_sq[60] = "<fssdsynthAddSqFromEE> can't load sq to midi csl. - res=%d\n";
char fssynth_msg_rmbank_idx[51] = "<fssdsynthRemoveBank> wong bank index. - index=%d\n";
char fssynth_msg_rmbank_iop[44] = "<fssdsynthRemoveBank> error iop addr. - %x\n";
char fssynth_msg_rmbank_hd[51] = "<fssdsynthRemoveBank> hd data is't exist. - id=%d\n";
char fssynth_msg_addbank_full[49] = "<fssdsynthAddBankFromEE2> bank table over flow.\n";
char fssynth_msg_addbank_port[56] = "<fssdsynthAddBankFromEE2> wrong port number. - port=%d\n";
char fssynth_msg_addbank_alloc[48] = "<fssdsynthAddBankFromEE2> can't alloc iop heap\n";
char fssynth_msg_hd_addr[12] = "hd-addr=%p\n";
char fssynth_msg_addbank_hd[25] = "HD IOP Addr:0x%x...0x%x\n";
char fssynth_msg_addbank_mono[30] = "Mono HD IOP Addr:0x%x...0x%x\n";
char fssynth_msg_addbank_spu[57] = "<fssdsynthAddBankFromEE2> cannot allocate SPU2 memory...";
char fssynth_msg_addbank_dma[63] = "<fssdsynthAddBankFromEE2> can't transfer buffer. - ee-buff=%x\n";
char fssynth_msg_addbank_bd[60] = "<fssdsynthAddBankFromEE2> can't trans bd to spu2. - res=%d\n";
char fssynth_msg_addbank_hsyn[63] = "<fssdsynthAddBankFromEE2> can't load hd to hsyn csl. - res=%d\n";
char fssynth_msg_stereo_hd[52] = "<fslibsdsynth> can't load hd to hsyn csl. - res=%d\n";
char fssynth_msg_stereo_bank[24] = "<fslibsdsynth> Bank %d\n";
char fssynth_msg_transbuf[52] = "<fslibsdsynth> can't Transfer Buffer. - EE-buff:%x\n";
#endif

/* Oracle: the prologue saves of the original frame (s0..s(n-1) at 0x10 * r with sq, ra at ra_off with sd); a later
 * function at the same depth may read them as stale stack. Exact when entered from MIPS (bw thunk). */
static inline void synth_saves(int n, unsigned int ra_off)
{
#ifdef ACNB_SOURCEPORT
    (void)n;
    (void)ra_off;
#else
    for (int r = 0; r < n; r++) PS2_SAVED_GPR(16 + r, 0x10 * r);
    PS2_STACK(unsigned long long, saved_ra, ra_off);
    saved_ra = PS2_STALE_GPR(31);
#endif
}

/* Oracle: an fssdCallRpc* callee saves ra at (its frame - 0x10) = 0x10 below the caller's sp; the original's ra there is
 * the return address of the jal (fn + jal_off + 8). Rewrite it after the native call. */
#ifdef ACNB_SOURCEPORT
#define SYNTH_RPC_RA(fn, jal_off) ((void)0)
#define SYNTH_RPC_RA_AT(base, jal_off) ((void)(base))
#else
#define SYNTH_RPC_RA(fn, jal_off)                                   \
    do {                                                            \
        PS2_STACK(unsigned long long, rpc_ra_, -0x10);              \
        rpc_ra_ = PS2PTR(GFN(fn)) + (jal_off) + 8;                  \
    } while (0)
/* the same with the function's entry address as a value (helper shared by SetStereo / SetMono) */
#define SYNTH_RPC_RA_AT(base, jal_off)                              \
    do {                                                            \
        PS2_STACK(unsigned long long, rpc_ra_, -0x10);              \
        rpc_ra_ = (unsigned long long)((base) + (jal_off) + 8);     \
    } while (0)
#endif

/* the SE buffer: word 0 = used length, records from +4 */
static inline struct struct_001cc32a *synth_se_buf(void)
{
    return (struct struct_001cc32a *)HOSTPTR(s_esmData.se_strp);
}

/* the stereo test of SeOn / ChangeSeParam / SeStreamOn: global flag, then the bank's flag */
static inline int synth_is_stereo(const struct struct_417c91c5 *b)
{
    return s_esmData.stereo != 0 && b->stereo != 0;
}

void fssdsynthAssociateSpu2MemBlockIdWithPort(int id, unsigned int port_bits)
{
    for (int i = 0; i < 16; i++)
        if (port_bits & (1u << i)) s_iPortSpuMap[i] = id;
}

void fssdsynthAddSpu2MemoryBlock(int id, int iSpuAddr, int iSize)
{
    FsSpuMmng_AddMemBlock(id, iSpuAddr, iSize);
}

int fssdsynthSeVolume(int se_idx, int vol)
{
    return fssdsynthChangeSeParam(0, se_idx, vol);
}

int fssdsynthSePan(int se_idx, int pan)
{
    return fssdsynthChangeSeParam(1, se_idx, pan);
}

int fssdsynthSePitchBend(int se_idx, int bend)
{
    return fssdsynthChangeSeParam(2, se_idx, bend);
}

int fssdsynthSetCoreAttr(unsigned short entry, unsigned short value)
{
    return fssdCallRpc(4, 1, 0x1000, entry, value, 0, 0, 0, 0, 0, 0);
}

int fssdsynthSetParam(unsigned short reg, unsigned short value)
{
    return fssdCallRpc(4, 1, 0xd00, reg, value, 0, 0, 0, 0, 0, 0);
}

int fssdsynthGetTransStatus(int ch)
{
    return fssdCallRpc(4, 1, 0x800, ch, 0, 0, 0, 0, 0, 0, 0);
}

int fssdsynthSetMVolume(unsigned short vol)
{
    return fssdCallRpcInt(4, 1, 0x300, vol);
}

int fssdsynthPortSoundOff(unsigned int port)
{
    return fssdCallRpcInt(4, 1, 0x10300, (int)port);
}

int fssdsynthUpdateVoiceInfo(void)
{
    PS2_FRAME(0x20);
    synth_saves(1, 0x10);
    int r = fssdCallRpcStruct(4, 1, 0, 0xa0, 0x810200, nullptr);
    SYNTH_RPC_RA(fssdsynthUpdateVoiceInfo, 0x24);
    const unsigned char *src = (const unsigned char *)fssdGetCalledRpcResult();
    unsigned char *dst = (unsigned char *)&g_voice;
    for (int i = 0; i < 0xa0; i++) dst[i] = src[i];
    return r;
}

void fssdsynthPackSeStreamSurroundOn(int SurNum, int num, struct SE_STR_SUR_PACK *packP)
{
    PS2_FRAME(0x90);
    synth_saves(7, 0x70);
    (void)SurNum;
    int n = num < 0x7e ? num : 0x7e;
    int i = 0;
    if (n <= 0) return;
    do {
        ps2_memset(&s_svpBuf, 0, 0xc0);
        int k = 0;
        struct StSurroundVolume *out = s_svpBuf.survol;
        for (; k < 16 && i < n; i++, packP++) {
            struct struct_fc32f43b *e = &s_esmData.se_tbl[i];
            if (e->use == -1) continue;
            int b = e->bt_idx;
            if (b < 0 || b >= 16) continue;
            int vol_l, vol_r;
            SetSurroundVolume(&vol_l, &vol_r, packP->vol, -packP->pan, 0x7ff);
            out->index = (unsigned short)i;
            out->vol_l = (short)vol_l;
            out->vol_r = (short)vol_r;
            out->phd = s_esmData.bank_tbl[b].hd_iopp;
            out->prog_no = e->ch;
            out++;
            k++;
        }
        if (k > 0) {
            fssdCallRpcStruct(4, 1, 0xc0, 0, 0x411c00, &s_svpBuf);
            SYNTH_RPC_RA(fssdsynthPackSeStreamSurroundOn, 0x14c);
        }
    } while (i < n);
}

static void SetSurroundVolume(int *Vol_l, int *Vol_r, int n_velo, int n_pan, int max_pan)
{
    PS2_FRAME(0x50);
    synth_saves(4, 0x40);
    int v = n_velo;
    if (v >= 0x80) v = 0x7f;
    if (v < 0) v = 0;
    int pan = n_pan;
    if (max_pan < pan) pan = max_pan;
    if (pan < -max_pan) pan = -max_pan;
    if (pan >= 0) {
        *Vol_l = (v * (int)_Cosf(pan)) / 127;
        *Vol_r = (v * (int)_Sinf(pan)) / 127;
    } else {
        pan = -pan;
        *Vol_l = (v * (int)_Cosf(pan)) / 127;
        *Vol_r = (-v * (int)_Sinf(pan)) / 127;
    }
    if (*Vol_l < 0) {
        *Vol_l = -*Vol_l;
        *Vol_l = 0x8000 - *Vol_l;
    }
    if (*Vol_r < 0) {
        *Vol_r = -*Vol_r;
        *Vol_r = 0x8000 - *Vol_r;
    }
}

int fssdsynthSeStreamOff(int se_idx)
{
    PS2_FRAME(0x30);
    synth_saves(2, 0x20);
    if (s_esmData.running == 0) return -1;
    if (se_idx < 0 || se_idx >= 0x7e) {
        ps2_printf_d(fssynth_msg_se_idx, se_idx);
        return -1;
    }
    struct struct_001cc32a *sb = synth_se_buf();
    unsigned int len = sb->size;
    if (len + 0x10 >= 0x3fc) return -2;
    unsigned char *p = (unsigned char *)sb + 4 + len;
    struct struct_fc32f43b *e = &s_esmData.se_tbl[se_idx];
    if (e->use == -1) return 0;
    p[0] = 0xff;
    p[1] = (unsigned char)s_esmData.bank_tbl[(int)e->bt_idx].hsyn_port;
    p[2] = 0x0d;
    sub_SeStreamNoteOn(p + 3, (unsigned int)se_idx, (unsigned char)e->use, (unsigned char)e->ch, (unsigned char)e->key, 0,
                       0x40);
    sb->size += 0x10;
    e->use = -1;
    return 0;
}

int fssdsynthSeStreamOn_forSurround(int se_idx, int bt_idx, char bank, char prog, char key)
{
    return fssdsynthSeStreamOn(se_idx, bt_idx, bank, prog, key, 1, 0x3f);
}

int fssdsynthSeStreamOn(int se_idx, int bt_idx, char bank, char prog, char key, char vol, char pan)
{
    PS2_FRAME(0x30);
    synth_saves(2, 0x20);
    if (s_esmData.running == 0) return -1;
    struct struct_001cc32a *sb = synth_se_buf();
    unsigned int len = sb->size;
    if (len + 0x10 >= 0x3fc) {
        ps2_printf_d(fssynth_msg_se_room, (int)len);
        return -2;
    }
    unsigned char *p = (unsigned char *)sb + 4 + len;
    if (se_idx < 0 || se_idx >= 0x7e) {
        ps2_printf_d(fssynth_msg_se_id, se_idx);
        return -1;
    }
    struct struct_fc32f43b *e = &s_esmData.se_tbl[se_idx & 0x7f];
    e->bt_idx = (char)bt_idx;
    e->ch = prog;
    e->key = key;
    e->use = bank;
    struct struct_417c91c5 *b = &s_esmData.bank_tbl[bt_idx];
    p[0] = 0xff;
    p[1] = (unsigned char)b->hsyn_port;
    p[2] = 0x0d;
    if (!synth_is_stereo(b)) pan = 0x3f;
    sub_SeStreamNoteOn(p + 3, (unsigned int)se_idx, (unsigned char)bank, (unsigned char)prog, (unsigned char)key,
                       (unsigned char)vol, (unsigned char)pan);
    sb->size += 0x10;
    return se_idx;
}

static void sub_SeStreamNoteOn(unsigned char *dPtr, unsigned int id, unsigned char bank, unsigned char prog,
                               unsigned char note, unsigned char velo, unsigned char pan)
{
    unsigned int id32 = id + 0x10000000u;
    dPtr[0] = 0xfe;
    dPtr[1] = 1;
    dPtr[2] = (unsigned char)id32;
    dPtr[3] = (unsigned char)(id32 >> 8);
    dPtr[4] = (unsigned char)(id32 >> 16);
    dPtr[5] = (unsigned char)(id32 >> 24);
    dPtr[6] = (unsigned char)((bank & 0xf) | 0xa0);
    dPtr[7] = prog;
    dPtr[8] = note;
    dPtr[9] = velo;
    dPtr[10] = pan;
    dPtr[11] = 0;
    dPtr[12] = 0;
}

void fssdsynthDefineSeStream(int port, int f_bank)
{
    fssdCallRpc(4, 1, 0x11600, port, f_bank, 0, 0, 0, 0, 0, 0);
}

/* rev_tbl[idx] for idx 0..4: index 4 is cur_reverb (the record after the table) */
static inline struct struct_6bd03be9 *synth_rev(int idx)
{
    return (struct struct_6bd03be9 *)((unsigned char *)s_esmData.rev_tbl + idx * 0x18);
}

void fssdsynthSetReverb(int rt_idx)
{
    PS2_FRAME(0x10);
    synth_saves(0, 0);
    if (rt_idx < 0 || rt_idx >= 5) {
        ps2_printf_d(fssynth_msg_setrev_id, rt_idx);
        return;
    }
    const unsigned int *s = (const unsigned int *)synth_rev(rt_idx);
    unsigned int *d = (unsigned int *)&s_esmData.cur_reverb;
    for (int i = 0; i < 6; i++) d[i] = s[i];   /* lwc1 / swc1: bit copies (self-copy for index 4) */
    fssdCallRpcStruct(4, 1, 0x18, 4, 0x400700, &s_esmData.cur_reverb);
    SYNTH_RPC_RA(fssdsynthSetReverb, 0x9c);
}

int fssdsynthRemoveReverb(int rt_idx)
{
    PS2_FRAME(0x10);
    synth_saves(0, 0);
    if (rt_idx < 0 || rt_idx >= 5) {
        ps2_printf_d(fssynth_msg_rmrev_id, rt_idx);
        return -1;
    }
    struct struct_6bd03be9 *e = synth_rev(rt_idx);
    if (e->core == -1) {
        ps2_printf_d(fssynth_msg_rmrev_undef, rt_idx);
        return -1;
    }
    e->core = -1;
    FlushCache(0);
    return 0;
}

int fssdsynthAddReverb(int mode, short ldepth, short rdepth, short delay, short feedback, short lvol, short rvol,
                       int core)
{
    PS2_FRAME(0x20);
    synth_saves(1, 0x10);
    int i;
    for (i = 0; i < 4; i++)
        if (s_esmData.rev_tbl[i].core == -1) break;
    if (i >= 4) {
        ps2_printf(fssynth_msg_rev_full);
        return -2;
    }
    struct struct_6bd03be9 *e = &s_esmData.rev_tbl[i];
    e->core = core;
    e->mode = mode | 0x100;
    e->ldepth = ldepth;
    e->rdepth = rdepth;
    e->delay = delay;
    e->feedback = feedback;
    e->lvol = lvol;
    e->rvol = rvol;
    FlushCache(0);
    return i;
}

int fssdsynthSeFlush(void)
{
    PS2_FRAME(0x30);
    synth_saves(2, 0x20);
    if (s_esmData.running == 0) return -1;
    struct struct_001cc32a *sb = synth_se_buf();
    s_sddSeTrans.data = PS2PTR(s_esmData.se_strp);   /* the cached pointer value */
    s_sddSeTrans.addr = PS2PTR(s_esmData.iop_se_strp);
    s_sddSeTrans.mode = 0;
    s_sddSeTrans.size = (sb->size + 4 + 15) & ~15u;   /* incl. up to 15 residue bytes */
    unsigned int id = sceSifSetDma(&s_sddSeTrans, 1);
    if (id == 0) return -1;   /* len stays */
    while (sceSifDmaStat(id) >= 0) {}
    sb->size = 0;   /* before the RPC (jal delay slot) */
    fssdCallRpcVoid(4, 1, 0x10800);
    SYNTH_RPC_RA(fssdsynthSeFlush, 0xc4);
    return 0;
}

int fssdsynthChangeSeParam(int type, int se_idx, int param)
{
    PS2_FRAME(0x10);
    synth_saves(0, 0);
    if (s_esmData.running == 0) return -1;
    if (se_idx < 0 || se_idx >= 0x7e) {
        ps2_printf_d(fssynth_msg_se_idx, se_idx);
        return -1;
    }
    struct struct_001cc32a *sb = synth_se_buf();
    unsigned int len = sb->size;
    if (len + 10 >= 0x3fc) return -2;
    unsigned char *p = (unsigned char *)sb + 4 + len;
    struct struct_fc32f43b *e = &s_esmData.se_tbl[se_idx];
    if (e->use == -1) return 0;
    struct struct_417c91c5 *b = &s_esmData.bank_tbl[(int)e->bt_idx];
    p[0] = 0xff;
    p[1] = (unsigned char)b->hsyn_port;
    p[2] = 7;
    if (type == 2) {
        p[3] = 0xfd;
        p[4] = 2;
        p[5] = (unsigned char)e->ch;
        p[6] = (unsigned char)e->key;
        p[7] = (unsigned char)se_idx;
        p[8] = (unsigned char)(param & 0x7f);
        p[9] = (unsigned char)(((param >> 7) + 0x40) & 0x7f);
    } else if (type == 1) {
        if (!synth_is_stereo(b)) param = 0x3f;
        p[3] = 0xfd;
        p[4] = 1;
        p[5] = (unsigned char)e->ch;
        p[6] = (unsigned char)e->key;
        p[7] = (unsigned char)se_idx;
        p[8] = (unsigned char)param;
        p[9] = 0;
    } else if (type == 0) {
        p[3] = 0xfd;
        p[4] = 0;
        p[5] = (unsigned char)e->ch;
        p[6] = (unsigned char)e->key;
        p[7] = (unsigned char)se_idx;
        p[8] = (unsigned char)param;
        p[9] = 0;
    }   /* any other type: only `ff bk 07`, the 7 bytes after it stay stale (quirk) */
    sb->size += 10;
    return 0;
}

int fssdsynthSeOff(int se_idx)
{
    PS2_FRAME(0x10);
    synth_saves(0, 0);
    if (s_esmData.running == 0) return -1;
    if (se_idx < 0 || se_idx >= 0x7e) {
        ps2_printf_d(fssynth_msg_se_idx, se_idx);
        return -1;
    }
    struct struct_001cc32a *sb = synth_se_buf();
    unsigned int len = sb->size;
    if (len + 10 >= 0x3fc) return -2;
    unsigned char *p = (unsigned char *)sb + 4 + len;
    struct struct_fc32f43b *e = &s_esmData.se_tbl[se_idx];
    if (e->use == -1) return 0;
    p[0] = 0xff;
    p[1] = (unsigned char)s_esmData.bank_tbl[(int)e->bt_idx].hsyn_port;
    p[2] = 7;
    p[3] = 0xfd;
    p[4] = 0x10;
    p[5] = (unsigned char)e->ch;
    p[6] = (unsigned char)e->key;
    p[7] = (unsigned char)se_idx;
    p[8] = 0;
    p[9] = 0;
    sb->size += 10;
    e->use = -1;
    return 0;
}

int fssdsynthSeOn(int se_idx, int bt_idx, char ch, char prog, char key, char vol, short bend, char pan)
{
    PS2_FRAME(0x10);
    synth_saves(0, 0);
    if (s_esmData.running == 0) return -1;
    unsigned int n = 0x19;
    if (prog != -1) n += 2;
    struct struct_001cc32a *sb = synth_se_buf();
    unsigned int len = sb->size;
    if (len + n >= 0x3fc) {
        ps2_printf_d(fssynth_msg_se_room, (int)len);
        return -2;
    }
    unsigned char *p = (unsigned char *)sb + 4 + len;
    struct struct_417c91c5 *b = &s_esmData.bank_tbl[bt_idx];
    if (se_idx < 0 || se_idx >= 0x7e) {
        ps2_printf_d(fssynth_msg_se_id, se_idx);
        return -1;
    }
    struct struct_fc32f43b *e = &s_esmData.se_tbl[se_idx & 0x7f];
    e->bt_idx = (char)bt_idx;
    e->ch = ch;
    e->key = key;
    e->use = 1;
    p[0] = 0xff;
    p[1] = (unsigned char)b->hsyn_port;
    p[2] = (unsigned char)(n - 3);
    p += 3;
    if (prog != -1) {
        p[0] = (unsigned char)(ch + 0xc0);
        p[1] = (unsigned char)prog;
        p += 2;
    }
    if (!synth_is_stereo(b)) pan = 0x3f;
    int bd = bend;
    unsigned char lo = (unsigned char)(bd & 0x7f);
    unsigned char hi = (unsigned char)(((bd >> 7) + 0x40) & 0x7f);
    static const int kN = 22;
    unsigned char ev[kN] = {0xf9, 1, (unsigned char)ch, (unsigned char)pan, 0,                                  /* pan */
                            0xf9, 2, (unsigned char)ch, lo, hi,                                                /* bend */
                            0xf9, 0, (unsigned char)ch, (unsigned char)vol, 0,                                 /* volume */
                            0xfd, 0x10, (unsigned char)ch, (unsigned char)key, (unsigned char)se_idx, 0x7f, 0}; /* note on */
    for (int i = 0; i < kN; i++) p[i] = ev[i];
    sb->size += n;
    return se_idx;
}

/* ---- G3: SQ / bank / stereo ---- */

/* sq_tbl[idx] / bank_tbl[idx] by byte offset: the range checks allow sq index 4 (= se_tbl[0..3]) and callers pass
 * unchecked bank indices */
static inline struct struct_1c3a99cd *synth_sq(int idx)
{
    return (struct struct_1c3a99cd *)((unsigned char *)s_esmData.sq_tbl + idx * 0x10);
}

static inline struct struct_417c91c5 *synth_bank(int idx)
{
    return (struct struct_417c91c5 *)((unsigned char *)s_esmData.bank_tbl + idx * 0x20);
}

/* s_iPortSpuMap[port], unchecked (AddBankFromEE2 lets port 16 through) */
static inline int synth_port_spu(int port)
{
    const int *map = s_iPortSpuMap;
    return map[port];
}

static inline unsigned int synth_iop(const void *p)
{
    return PS2PTR(p);
}

/* RemoveSq's body (AddSqFromEE inlines it on its error paths): free the SQ's IOP copy and clear the slot */
static int synth_sq_free(int idx)
{
    struct struct_1c3a99cd *e = synth_sq(idx);
    if (e->iopp == 0) {
        ps2_printf_d(fssynth_msg_rmsq_undef, idx);
        return -1;
    }
    if (sceSifFreeIopHeap(e->iopp) != 0) {
        ps2_printf_d(fssynth_msg_rmsq_iop, (int)synth_iop(e->iopp));
        return -1;
    }
    e->iopp = 0;
    e->size = 0;
    FlushCache(0);
    return 0;
}

int fssdsynthSqVolume(int st_idx, short vol)
{
    return fssdCallRpc(4, 1, 0x10600, synth_sq(st_idx)->midi_port, (int)vol, 0, 0, 0, 0, 0, 0);
}

int fssdsynthStopSq(int st_idx)
{
    PS2_FRAME(0x20);
    synth_saves(1, 0x10);
    if (st_idx < 0 || st_idx >= 5) {
        ps2_printf_d(fssynth_msg_stopsq_idx, st_idx);
        return -1;
    }
    struct struct_1c3a99cd *e = synth_sq(st_idx);
    if (e->iopp == 0) {
        ps2_printf_d(fssynth_msg_stopsq_undef, st_idx);
        return -1;
    }
    int r = fssdCallRpc(4, 1, 0x20400, e->midi_port, 0, 0, 0, 0, 0, 0, 0);
    SYNTH_RPC_RA(fssdsynthStopSq, 0x80);
    if (r != 0) {
        ps2_printf_dd(fssynth_msg_stopsq_fail, st_idx, r);
        return -1;
    }
    return 0;
}

int fssdsynthCheckSqEos(int st_idx)
{
    PS2_FRAME(0x10);
    synth_saves(0, 0);
    if (st_idx < 0 || st_idx >= 5) {
        ps2_printf_d(fssynth_msg_eos_idx, st_idx);   /* a1 = st_idx from the entry move */
        return -1;
    }
    int r = fssdCallRpc(4, 1, 0x20100, synth_sq(st_idx)->midi_port, 0, 0, 0, 0, 0, 0, 0);
    SYNTH_RPC_RA(fssdsynthCheckSqEos, 0x58);
    return r;
}

int fssdsynthPlaySq(int st_idx, int count)
{
    PS2_FRAME(0x20);
    synth_saves(1, 0x10);
    if (s_esmData.running == 0) return -1;
    if (st_idx < 0 || st_idx >= 5) {
        ps2_printf_d(fssynth_msg_playsq_idx, st_idx);
        return -1;
    }
    struct struct_1c3a99cd *e = synth_sq(st_idx);
    if (e->iopp == 0) {
        ps2_printf_d(fssynth_msg_playsq_undef, st_idx);
        return -1;
    }
    int r = fssdCallRpc(4, 1, 0x20300, e->midi_port, count, 0, 0, 0, 0, 0, 0);
    SYNTH_RPC_RA(fssdsynthPlaySq, 0x98);
    if (r != 0) {
        ps2_printf_dd(fssynth_msg_playsq_fail, st_idx, r);
        return -1;
    }
    return 0;
}

int fssdsynthRemoveSq(int st_idx)
{
    PS2_FRAME(0x20);
    synth_saves(1, 0x10);
    return synth_sq_free(st_idx);   /* no range check */
}

int fssdsynthAddSqFromEE(int port, int bt_idx, char *sq_buff, unsigned int sq_size)
{
    PS2_FRAME(0x50);
    synth_saves(4, 0x40);
    int i;
    for (i = 0; i < 4; i++)
        if (s_esmData.sq_tbl[i].iopp == 0) break;
    if (i >= 4) {
        ps2_printf(fssynth_msg_addsq_full);
        return -1;
    }
    if (port == -1) {
        port = i;
    } else if (port < 0 || port >= 4) {
        ps2_printf_d(fssynth_msg_addsq_port, port);
        return -2;
    }
    struct struct_1c3a99cd *e = &s_esmData.sq_tbl[i];
    e->midi_port = port;
    e->bt_idx = bt_idx;
    struct struct_417c91c5 *b = synth_bank(e->bt_idx);
    e->size = (int)((sq_size + 15) & ~15u);
    e->iopp = sceSifAllocIopHeap((unsigned int)e->size);
    if (e->iopp == 0 || synth_iop(e->iopp) + (unsigned int)e->size >= 0x200000u) {
        ps2_printf(fssynth_msg_addsq_alloc);
        ps2_printf_d(fssynth_msg_addr, (int)PS2PTR(sq_buff));
        synth_sq_free(i);
        return -3;
    }
    /* a1 / a2 still hold iopp and iopp + size from the range test */
    ps2_printf_dd(fssynth_msg_addsq_iop, (int)synth_iop(e->iopp), (int)(synth_iop(e->iopp) + (unsigned int)e->size));
    fssdsynthSifTransBuffer((char *)e->iopp, sq_buff, (unsigned int)e->size);
    while (fssdsynthSifTransSync() == 0) {}
    void *h = b->stereo ? b->hd_iopp : b->hd_m_iopp;
    int r = fssdCallRpc(4, 1, 0x10100, e->midi_port, (int)synth_iop(b->bd_spup), (int)synth_iop(h), (int)b->bank, 0, 0,
                        0, 0);
    SYNTH_RPC_RA(fssdsynthAddSqFromEE, 0x218);
    if (r != 0) {
        ps2_printf_d(fssynth_msg_addsq_hd, r);
        ps2_printf_d(fssynth_msg_addsq_bank, (int)b->bank);
        synth_sq_free(i);
        return -4;
    }
    r = fssdCallRpc(4, 1, 0x20000, e->midi_port, (int)synth_iop(e->iopp), 0, 0, 0, 0, 0, 0);
    SYNTH_RPC_RA(fssdsynthAddSqFromEE, 0x2d0);
    if (r != 0) {
        ps2_printf_d(fssynth_msg_addsq_sq, r);
        synth_sq_free(i);
        return -5;
    }
    FlushCache(0);
    return i;
}

int fssdsynthRemoveBank(int bt_idx)
{
    PS2_FRAME(0x30);
    synth_saves(2, 0x20);
    if (bt_idx < 0 || bt_idx >= 0x10) {
        ps2_printf_d(fssynth_msg_rmbank_idx, bt_idx);
        return -1;
    }
    for (int k = 0; k < 0x7e; k++) {
        struct struct_fc32f43b *e = &s_esmData.se_tbl[k];
        if (e->bt_idx == bt_idx) {
            e->use = -1;
            e->key = -1;
            e->ch = -1;
            e->bt_idx = -1;
        }
    }
    struct struct_417c91c5 *b = &s_esmData.bank_tbl[bt_idx];
    FsSpuMmng_Free(synth_port_spu(b->hsyn_port), (int)synth_iop(b->bd_spup));   /* also when bd_spup is 0 */
    b->bd_spup = 0;
    if (b->bd_iopp != 0) {
        if (sceSifFreeIopHeap(b->bd_iopp) != 0) {
            ps2_printf_d(fssynth_msg_rmbank_iop, (int)synth_iop(b->bd_iopp));
            return -1;
        }
        b->bd_iopp = 0;
    }
    if (b->hd_iopp != 0) {
        if (sceSifFreeIopHeap(b->hd_iopp) != 0) {
            ps2_printf_d(fssynth_msg_rmbank_iop, (int)synth_iop(b->hd_iopp));
            return -1;
        }
        b->hd_iopp = 0;
    } else {
        ps2_printf_d(fssynth_msg_rmbank_hd, bt_idx);
    }
    if (b->hd_m_iopp != 0) {
        if (sceSifFreeIopHeap(b->hd_m_iopp) != 0) {
            ps2_printf_d(fssynth_msg_rmbank_iop, (int)synth_iop(b->hd_m_iopp));
            return -1;
        }
        b->hd_m_iopp = 0;
    } else {
        ps2_printf_d(fssynth_msg_rmbank_hd, bt_idx);
    }
    FlushCache(0);
    return 0;
}

int fssdsynthAddBankFromEE2(int port, char *hd_buff, unsigned int hd_size, char *bd_buff, unsigned int bd_size, int bank)
{
    PS2_FRAME(0x90);
    synth_saves(8, 0x80);
    int slot;
    for (slot = 0; slot < 16; slot++)
        if (s_esmData.bank_tbl[slot].hd_iopp == 0) break;
    if (slot >= 16) {
        ps2_printf(fssynth_msg_addbank_full);
        return -1;
    }
    if (port == -1) {
        port = slot;
    } else if (port < 0 || port >= 0x11) {   /* 16 passes (quirk) */
        ps2_printf_d(fssynth_msg_addbank_port, port);
        return -2;
    }
    struct struct_417c91c5 *b = &s_esmData.bank_tbl[slot];
    b->hsyn_port = (short)port;
    b->bank = (char)bank;
    b->hd_size = (int)((hd_size + 15) & ~15u);
    b->hd_iopp = sceSifAllocIopHeap((unsigned int)b->hd_size);
    b->hd_m_iopp = sceSifAllocIopHeap((unsigned int)b->hd_size);
    if (b->hd_iopp == 0 || b->hd_m_iopp == 0 || synth_iop(b->hd_iopp) + (unsigned int)b->hd_size >= 0x200000u ||
        synth_iop(b->hd_m_iopp) + (unsigned int)b->hd_size >= 0x200000u) {
        ps2_printf(fssynth_msg_addbank_alloc);
        ps2_printf_d(fssynth_msg_hd_addr, (int)PS2PTR(hd_buff));
        fssdsynthRemoveBank(slot);
        return -3;
    }
    /* a1 / a2 still hold hd_iopp and hd_iopp + hd_size from the range test */
    ps2_printf_dd(fssynth_msg_addbank_hd, (int)synth_iop(b->hd_iopp),
                  (int)(synth_iop(b->hd_iopp) + (unsigned int)b->hd_size));
    ps2_printf_dd(fssynth_msg_addbank_mono, (int)synth_iop(b->hd_m_iopp),
                  (int)(synth_iop(b->hd_m_iopp) + (unsigned int)b->hd_size));
    fssdsynthSifTransBuffer((char *)b->hd_iopp, hd_buff, (unsigned int)b->hd_size);
    while (fssdsynthSifTransSync() == 0) {}
    /* the IOP builds the mono HD copy: w0 = mono dst, w1 = stereo src (result ignored) */
    fssdCallRpc(4, 1, 0x10700, (int)synth_iop(b->hd_m_iopp), (int)synth_iop(b->hd_iopp), b->hd_size, 0, 0, 0, 0, 0);
    SYNTH_RPC_RA(fssdsynthAddBankFromEE2, 0x1e8);
    int left = (int)((bd_size + 15) & ~15u);
    b->bd_size = left;
    if (b->bd_spup == 0)
        b->bd_spup = (void *)(uintptr_t)(unsigned int)FsSpuMmng_Alloc(synth_port_spu(port), b->bd_size);
    unsigned int spu = synth_iop(b->bd_spup);
    if (spu == 0) {
        ps2_printf(fssynth_msg_addbank_spu);
        fssdsynthRemoveBank(slot);
        return -1;
    }
    /* BD chunk buffer: halve from 1 MB until the IOP heap has room (first try 0x80000, no lower bound) */
    unsigned int csize = 0x100000;
    void *cbuf;
    do {
        csize >>= 1;
        cbuf = sceSifAllocIopHeap(csize);
    } while (cbuf == 0);
    unsigned int src = PS2PTR(bd_buff);
    if (left > 0) {
        do {
            s_sddPacket.size = csize < (unsigned int)left ? csize : (unsigned int)left;
            s_sddPacket.data = src;
            s_sddPacket.addr = synth_iop(cbuf);
            s_sddPacket.mode = 0;
            fssdCallRpc(4, 1, 0x800, (int)b->ch, 1, 0, 0, 0, 0, 0, 0);
            SYNTH_RPC_RA(fssdsynthAddBankFromEE2, 0x2c0);
            unsigned int id = sceSifSetDma(&s_sddPacket, 1);
            if (id == 0) {
                ps2_printf_d(fssynth_msg_addbank_dma, (int)src);
                sceSifFreeIopHeap(cbuf);
                fssdsynthRemoveBank(slot);
                return -5;
            }
            while (sceSifDmaStat(id) >= 0) {}
            unsigned int chunk = s_sddPacket.size;
            unsigned int want = chunk;
            if (chunk & 0x3f) want = ((chunk >> 6) + 1) << 6;
            /* w4 = t3 = the chunk size (left in t3 by the rounding) */
            int r = fssdCallRpc(4, 1, 0x900, (int)b->ch, 0, (int)synth_iop(cbuf), (int)spu, (int)chunk, 0, 0, 0);
            SYNTH_RPC_RA(fssdsynthAddBankFromEE2, 0x360);
            if ((unsigned int)r != want) {
                ps2_printf_d(fssynth_msg_addbank_bd, r);
                sceSifFreeIopHeap(cbuf);
                fssdsynthRemoveBank(slot);
                return -1;
            }
            unsigned int adv = s_sddPacket.size;
            left -= (int)adv;
            src += adv;
            spu += adv;   /* the unrounded chunk (quirk) */
        } while (left > 0);
    }
    fssdCallRpc(4, 1, 0x800, (int)b->ch, 1, 0, 0, 0, 0, 0, 0);
    SYNTH_RPC_RA(fssdsynthAddBankFromEE2, 0x3c0);
    sceSifFreeIopHeap(cbuf);
    void *h = b->stereo ? b->hd_iopp : b->hd_m_iopp;
    int r = fssdCallRpc(4, 1, 0x10100, (int)b->hsyn_port, (int)synth_iop(b->bd_spup), (int)synth_iop(h), (int)b->bank, 0,
                        0, 0, 0);
    SYNTH_RPC_RA(fssdsynthAddBankFromEE2, 0x3fc);
    if (r != 0) {
        ps2_printf_d(fssynth_msg_addbank_hsyn, r);
        ps2_printf_d(fssynth_msg_bank, (int)b->bank);
        fssdsynthRemoveBank(slot);
        return -6;
    }
    FlushCache(0);
    return slot;
}

/* SetStereo / SetMono (st = 1 / 0): same code, the HD copy re-registered is hd_iopp / hd_m_iopp; base = the
 * function's entry (both have their jals at the same offsets) */
static void synth_set_output(unsigned int st, unsigned int base)
{
    (void)base;
    if (s_esmData.running != 0) {
        fssdCallRpcVoid(4, 1, 0x500);
        SYNTH_RPC_RA_AT(base, 0x28);
    }
    for (int k = 0; k < 16; k++) {
        fssdCallRpc(4, 1, 0x11100, k, 0, 0, 0, 0, 0, 0, 0);
        SYNTH_RPC_RA_AT(base, 0x48);
    }
    s_esmData.stereo = (unsigned short)st;
    fssdCallRpcInt(4, 1, 0xc00, (int)st);
    SYNTH_RPC_RA_AT(base, 0x70);
    /* bank pass: p advances only after a re-registration (an empty / incomplete slot freezes it, quirk) */
    struct struct_417c91c5 *p = s_esmData.bank_tbl;
    for (int k = 0; k < 16; k++) {
        p->stereo = st;
        if (p->hd_iopp != 0 && p->bd_spup != 0 && p->hd_m_iopp != 0) {
            void *h = st ? p->hd_iopp : p->hd_m_iopp;
            int r = fssdCallRpc(4, 1, 0x10100, (int)p->hsyn_port, (int)synth_iop(p->bd_spup), (int)synth_iop(h),
                                (int)p->bank, 0, 0, 0, 0);
            SYNTH_RPC_RA_AT(base, 0xc4);
            if (r != 0) {
                ps2_printf_d(fssynth_msg_stereo_hd, r);
                ps2_printf_d(fssynth_msg_stereo_bank, (int)p->bank);
            }
            p++;
        }
    }
    /* SQ pass, same sticking pointer; w4 = the mode */
    struct struct_1c3a99cd *q = s_esmData.sq_tbl;
    for (int k = 0; k < 4; k++) {
        if (q->iopp != 0 && q->bt_idx != -1) {
            struct struct_417c91c5 *b = synth_bank(q->bt_idx);
            void *h = st ? b->hd_iopp : b->hd_m_iopp;
            int r = fssdCallRpc(4, 1, 0x20700, q->midi_port, (int)synth_iop(b->bd_spup), (int)synth_iop(h), (int)b->bank,
                                (int)st, 0, 0, 0);
            SYNTH_RPC_RA_AT(base, 0x164);
            if (r != 0) {
                ps2_printf_d(fssynth_msg_stereo_hd, r);
                ps2_printf_d(fssynth_msg_stereo_bank, (int)b->bank);
            }
            q++;
        }
    }
    if (s_esmData.running != 0) {
        fssdCallRpcVoid(4, 1, 0x400);
        SYNTH_RPC_RA_AT(base, 0x1b8);
    }
}

void fssdsynthSetStereo(void)
{
    PS2_FRAME(0x40);
    synth_saves(3, 0x30);
    synth_set_output(1, PS2PTR(GFN(fssdsynthSetStereo)));
}

void fssdsynthSetMono(void)
{
    PS2_FRAME(0x40);
    synth_saves(3, 0x30);
    synth_set_output(0, PS2PTR(GFN(fssdsynthSetMono)));
}

/* ---- G4: system start / quit / init, chunked SIF transfer, table sine ---- */

void fssdsynthSystemStart(void)
{
    PS2_FRAME(0x10);
    synth_saves(0, 0);
    if (s_esmData.running != 1) {
        fssdCallRpcVoid(4, 1, 0x400);
        SYNTH_RPC_RA(fssdsynthSystemStart, 0x1c);
        s_esmData.running = 1;
    }
}

/* SifTransBuffer / Sync: the DMA of the current chunk (trans_size = 16-rounded, so up to 15 bytes past the chunk ride
 * along) always goes to the IOP packet buffer iop_pktp; the next Sync call's RPC 0xb00 has the IOP copy it to d_buff.
 * s_sddPacket is shared with AddBankFromEE2's BD transfer. */
static unsigned int synth_trans_chunk(void)
{
    s_sddPacket.mode = 0;
    s_sddPacket.data = PS2PTR(s_sdiData.s_buff);
    s_sddPacket.addr = PS2PTR(s_esmData.iop_pktp);
    s_sddPacket.size = (unsigned int)s_sdiData.trans_size;   /* jal delay slot of FlushCache */
    FlushCache(0);
    s_sdiData.did = (int)sceSifSetDma(&s_sddPacket, 1);
    return (unsigned int)s_sdiData.did;
}

/* Advances one 0x4000 chunk per call and adds no wait of its own: the callers' `while (!SifTransSync()) {}` loops end
 * because sceSifDmaStat reports completion on the first poll and the RPC is synchronous. A failed SetDma is not
 * signalled (returns 0, stat stays 1; quirk). */
int fssdsynthSifTransSync(void)
{
    PS2_FRAME(0x10);
    synth_saves(0, 0);
    if (s_sdiData.stat == 0) return 1;
    if (sceSifDmaStat((unsigned int)s_sdiData.did) >= 0) return 0;
    /* w0 = IOP destination, w1 = this chunk's unrounded size (both read before the call) */
    fssdCallRpc(4, 1, 0xb00, (int)PS2PTR(s_sdiData.d_buff), s_sdiData.real_size, 0, 0, 0, 0, 0, 0);
    SYNTH_RPC_RA(fssdsynthSifTransSync, 0x58);
    s_sdiData.remain_size = s_sdiData.remain_size - s_sdiData.real_size;
    int remain = s_sdiData.remain_size;
    if (remain <= 0) {
        s_sdiData.stat = 0;
        return 1;
    }
    s_sdiData.s_buff = s_sdiData.s_buff + 0x4000;
    s_sdiData.d_buff = s_sdiData.d_buff + 0x4000;
    if (remain < 0x4001) {
        s_sdiData.trans_size = remain;
        s_sdiData.real_size = remain;
        s_sdiData.trans_size = (s_sdiData.trans_size + 15) & -16;
    } else {
        s_sdiData.trans_size = 0x4000;
        s_sdiData.real_size = 0x4000;
    }
    if (synth_trans_chunk() == 0) ps2_printf_d(fssynth_msg_transbuf, (int)PS2PTR(s_sdiData.s_buff));
    return 0;
}

int fssdsynthSifTransBuffer(char *iop_buff, char *ee_buff, unsigned int size)
{
    PS2_FRAME(0x10);
    synth_saves(0, 0);
    s_sdiData.s_buff = ee_buff;    /* guest address as a value */
    s_sdiData.d_buff = iop_buff;   /* IOP address as a value */
    s_sdiData.stat = 1;
    s_sdiData.remain_size = (int)size;
    if (size < 0x4001) {   /* unsigned */
        s_sdiData.trans_size = (int)size;
        s_sdiData.real_size = (int)size;
        s_sdiData.trans_size = (s_sdiData.trans_size + 15) & -16;
    } else {
        s_sdiData.trans_size = 0x4000;
        s_sdiData.real_size = 0x4000;
    }
    if (synth_trans_chunk() == 0) {
        ps2_printf_d(fssynth_msg_transbuf, (int)PS2PTR(s_sdiData.s_buff));
        return -1;
    }
    return 0;
}

/* Quirks kept: banks with hd_iopp == 0 are skipped even if bd_spup is set; nothing but running is reset. */
void fssdsynthQuit(void)
{
    PS2_FRAME(0x50);
    synth_saves(4, 0x40);
    if (s_esmData.running != 0) {
        fssdCallRpcVoid(4, 1, 0x500);
        SYNTH_RPC_RA(fssdsynthQuit, 0x2c);
        s_esmData.running = 0;
    }
    for (int k = 0; k < 4; k++)
        if (s_esmData.sq_tbl[k].iopp != 0) synth_sq_free(k);   /* RemoveSq's body inlined behind the guard */
    for (int k = 0; k < 16; k++)
        if (s_esmData.bank_tbl[k].hd_iopp != 0) fssdsynthRemoveBank(k);
    fssdCallRpcVoid(4, 1, 0x200);
    SYNTH_RPC_RA(fssdsynthQuit, 0x118);
    FlushCache(0);
}

/* Left as is (quirk): bank_tbl[].hd_size, sq_tbl[].bt_idx, cur_reverb, the rest of s_sdiData. */
int fssdsynthInit(void)
{
    PS2_FRAME(0x10);
    synth_saves(0, 0);
    fssdCallRpcVoid(4, 1, 0x100);
    SYNTH_RPC_RA(fssdsynthInit, 0x10);
    s_esmData.iop_se_strp = (void *)(uintptr_t)(unsigned int)fssdCallRpcVoid(4, 1, 0x11d00);
    SYNTH_RPC_RA(fssdsynthInit, 0x24);
    s_esmData.mvol = 0x3fff;
    /* ssize 0x20 form (not SetMVolume's RpcInt): w0 = w1 = mvol */
    fssdCallRpc(4, 1, 0x300, (int)s_esmData.mvol, (int)s_esmData.mvol, 0, 0, 0, 0, 0, 0);
    SYNTH_RPC_RA(fssdsynthInit, 0x54);
    s_esmData.top_iopp = 0;   /* jal delay slot: before the call */
    /* no named word; w0 = a3 as the 0x300 call leaves it (fssdCallRpc's sceSifCallRpc send buffer), kept because it
     * shows in the trace's s0; the other words are residue (0 here) */
    s_esmData.iop_pktp = (void *)(uintptr_t)(unsigned int)fssdCallRpc(4, 1, 0xa00, (int)PS2PTR(g_ucEeToIopRpcBuff), 0, 0,
                                                                     0, 0, 0, 0, 0);
    SYNTH_RPC_RA(fssdsynthInit, 0x6c);
    s_esmData.se_strp = (struct struct_001cc32a *)s_ucSeBuf;   /* the cached address */
    synth_se_buf()->size = 0;
    s_esmData.stereo = 1;
    s_esmData.running = 0;
    for (int k = 0; k < 16; k++) {
        struct struct_417c91c5 *b = &s_esmData.bank_tbl[k];
        b->hd_iopp = 0;
        b->hd_m_iopp = 0;
        b->bd_iopp = 0;
        b->bd_spup = 0;
        b->bd_size = 0;
        b->ch = 0;
        b->hsyn_port = 0;
        b->bank = 0;
        b->stereo = 1;
    }
    for (int k = 0; k < 4; k++) {
        struct struct_1c3a99cd *q = &s_esmData.sq_tbl[k];
        q->iopp = 0;
        q->midi_port = 0;
        q->size = 0;
    }
    for (int k = 0; k < 126; k++) {
        struct struct_fc32f43b *e = &s_esmData.se_tbl[k];
        e->use = -1;
        e->key = -1;
        e->ch = -1;
        e->bt_idx = -1;
    }
    for (int k = 0; k < 4; k++) {
        struct struct_6bd03be9 *r = &s_esmData.rev_tbl[k];
        r->core = -1;
        r->mode = 0;
        r->ldepth = 0;
        r->rdepth = 0;
        r->delay = 0;
        r->feedback = 0;
        r->lvol = 0;
        r->rvol = 0;
    }
    FsSpuMmng_Init();
    s_sdiData.stat = 0;   /* jal delay slot of FlushCache */
    FlushCache(0);
    return 0;
}

/* Quarter-wave sine table: entry i = sin(i * 4 units), 2048 units per turn, 0x4000 = 1.0 (small maths table, kept in
 * code; crc32 of the original 256 bytes at 0x259ba0 = 0x24d18d0c). Hybrid: the DWARF static (acnb/local header). */
#ifdef ACNB_SOURCEPORT
short s_SinTable[128] = {
    0, 202, 405, 607, 810, 1012, 1214, 1416, 1618, 1820, 2021, 2222,
    2422, 2623, 2822, 3022, 3221, 3419, 3617, 3814, 4011, 4207, 4403, 4598,
    4792, 4985, 5178, 5370, 5561, 5751, 5940, 6129, 6316, 6503, 6688, 6873,
    7056, 7238, 7420, 7600, 7779, 7956, 8133, 8308, 8482, 8655, 8826, 8996,
    9165, 9332, 9498, 9663, 9825, 9987, 10147, 10305, 10462, 10617, 10770, 10922,
    11073, 11221, 11368, 11513, 11656, 11798, 11937, 12075, 12211, 12345, 12478, 12608,
    12737, 12863, 12988, 13110, 13231, 13349, 13466, 13580, 13692, 13802, 13911, 14017,
    14120, 14222, 14322, 14419, 14514, 14607, 14698, 14786, 14872, 14956, 15038, 15117,
    15194, 15268, 15341, 15411, 15478, 15544, 15606, 15667, 15725, 15781, 15834, 15885,
    15933, 15979, 16023, 16064, 16102, 16138, 16172, 16203, 16232, 16258, 16282, 16303,
    16322, 16338, 16352, 16363, 16372, 16378, 16382, 16383,
};
#endif

/* The body _Sinf and _Cosf share. Quirks kept: a & 0x1ff wraps negative arguments (_Sinf(-1) ~ +1.0), quadrants
 * outside 0..3 (a >= 0x800, a <= -0x200) give 0. `/ 4` is C signed division. */
static short synth_sin(int a)
{
    const short *T = s_SinTable;
    int m = a & 0x1ff;
    int i = m >> 2;
    int w = 4 - (m & 3);
    int q = a >= 0 ? a >> 9 : (a + 0x1ff) >> 9;
    int v;
    switch (q) {
    case 0:
    case 2:
        v = i < 127 ? T[i + 1] + (w * (T[i] - T[i + 1])) / 4 : T[127];
        break;
    case 1:
    case 3:
        v = i > 0 ? T[127 - i] + (w * (T[128 - i] - T[127 - i])) / 4 : T[127];
        break;
    default:
        return 0;
    }
    if (q >= 2) v = -v;
    return (short)v;
}

short _Sinf(int theta)
{
    return synth_sin(theta);
}

short _Cosf(int theta)
{
    int x = (int)((unsigned int)theta + 0x200u);
    if (x >= 0x800) x -= 0x800;
    return synth_sin(x);
}
