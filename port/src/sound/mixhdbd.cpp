/* src/sound/mixhdbd.cpp - merges two Sony sound-driver banks (HD chunk container "Vers / Head / Vagi / Smpl / Sset /
 * Prog" + BD ADPCM body, each preceded by an "SDU" header: HD / BD offsets and sizes, then a 0x2000-byte table of
 * the sound-source ids it holds) - the common SE bank and the stage's - into one bank plus tblSss (sound-source id
 * -> merged program index). Stage_Load registers the result through DF_SND_Setting_MAPSE. Programs are merged in
 * ascending id order; an id present in both files is taken from the first; id 0 is never taken. The chunk tags are
 * stored byte-reversed ("IECS" "igaV" = SCEI Vagi read as little-endian words). The original works in one large
 * stack frame and writes past its tables in edge cases (an empty file writes entry[-1]; more listed ids than
 * samples run into the next table), so the frame is kept as the PS2 stack block. */
#include <string.h>

namespace {
struct mix_entry {
    short sss;
    short pad;
    int size;   /* BD bytes */
    int off;    /* BD offset in its file */
};
struct mix_order {
    short file;
    short idx;
};
struct mix_frame {   /* the original frame from sp + 0x100 */
    char chunk[4][0x10];   /* Vagi, Smpl, Sset, Prog chunk headers */
    char head[0x40];       /* the rebuilt Head chunk */
    mix_order order[64];
    mix_entry tab[2][64];
    struct_3eb8295f ctx[2];
    int pad8f0[2];
    int count[2];
    int bdSize[2];
    int hdSize[2];
    struct_3eb8295f *ctxp[2];
    char *sdu2[2];
    char *sdu1[2];
    int pad928;
    int scratch;           /* running offset / step counter */
};
static_assert(sizeof(void *) != 4 || sizeof(mix_frame) == 0x830, "mix_frame layout");
}

/* find the chunks of an HD container: every tag found counts as one byte of the scan; returns the sample count
 * (last Smpl index + 1) or -1 when fewer than 5 bytes were scanned */
static int __AttachSecond(struct_3eb8295f *pSecond, int sizeHd)
{
    PS2_FRAME(0x50);
    int i = 0;
    char *p = pSecond->h_top;
    for (;;) {
        if (!strncmp(p, "IECSsreV", 8)) {
            pSecond->pVers = p;
            i++;
            p += 8;
            continue;
        }
        if (!strncmp(p, "IECSigaV", 8)) {
            pSecond->pVagi = p;
            i++;
            p += 8;
            continue;
        }
        if (!strncmp(p, "IECSlpmS", 8)) {
            pSecond->pSmpl = p;
            i++;
            p += 8;
            continue;
        }
        if (!strncmp(p, "IECStesS", 8)) {
            pSecond->pSset = p;
            i++;
            p += 8;
            continue;
        }
        if (!strncmp(p, "IECSdaeH", 8)) {
            pSecond->pHead = p;
            i++;
            p += 8;
            continue;
        }
        if (!strncmp(p, "IECSgorP", 8)) {
            pSecond->pProg = p;
            i++;
            p += 8;
            continue;
        }
        i++;
        p += 1;
        if (sizeHd < i) break;
    }
    if (i < 5) return -1;
    return *(int *)(pSecond->pSmpl + 0xc) + 1;
}

static inline void mix_chunk_header(char *h, const char *tag, int size, int n)
{
    strncpy(h, "IECS", 4);
    strncpy(h + 4, tag, 4);
    *(int *)(h + 8) = size;
    *(int *)(h + 0xc) = n - 1;
}

void MixHdBd_Mix(char *pbufUniHd, int *psizeUniHd, char *pbufUniBd, int *psizeUniBd, short *tblSss, char *pbufSdu1,
                 char *pbufSdu2)
{
    PS2_FRAME(0x930);
    PS2_STACK(mix_frame, fr, 0x100);
    fr.sdu1[0] = pbufSdu1;
    fr.sdu1[1] = pbufSdu2;
    fr.sdu2[0] = pbufSdu1;
    fr.sdu2[1] = pbufSdu2;
    fr.ctxp[0] = &fr.ctx[0];
    fr.ctxp[1] = &fr.ctx[1];

    /* A: locate both containers' chunks and tables */
    int total = 0;
    for (int k = 0; k < 2; k++) {
        struct_3eb8295f *c = fr.ctxp[k];
        c->h_top = fr.sdu1[k] + ((int *)fr.sdu2[k])[0];
        c->b_top = fr.sdu1[k] + ((int *)fr.sdu2[k])[1];
        fr.hdSize[k] = ((int *)fr.sdu2[k])[2];
        fr.bdSize[k] = ((int *)fr.sdu2[k])[3];
        fr.count[k] = __AttachSecond(c, fr.hdSize[k]);
        fr.scratch = *(int *)(c->pVagi + 0x10);
        c->sf.vagi = c->pVagi + fr.scratch;
        fr.scratch = *(int *)(c->pSmpl + 0x10);
        c->sf.smpl = c->pSmpl + fr.scratch;
        fr.scratch = *(int *)(c->pSset + 0x10);
        c->sf.sset = c->pSset + fr.scratch;
        fr.scratch = *(int *)(c->pProg + 0x10);
        c->sf.prog = c->pProg + fr.scratch;
        c->sf.BDoffs = 0;
        c->sf.sz_BD = *(unsigned int *)(c->pHead + 0x10);
        total += fr.count[k];
    }
    if (!(total < 0x40)) return;

    /* the ids each file lists, then their BD offsets / sizes from the Vagi tables */
    ZeroMemory(fr.tab, 0x600);
    ZeroMemory(fr.order, 0x100);
    for (int k = 0; k < 2; k++) {
        mix_entry *e = &fr.tab[0][0] + k * 64;
        char *ids = fr.sdu1[k] + 0x10;
        for (int j = 0; j < 0x2000; j++) {
            if (ids[j]) {
                e->sss = (short)j;
                e++;
            }
        }
    }
    fr.scratch = 0;
    for (int k = 0; k < 2; k++) {
        mix_entry *e = &fr.tab[0][0] + k * 64;
        char *vagi = fr.ctxp[k]->sf.vagi;
        int i = 0;
        for (; i < fr.count[k]; i++, vagi += 0xc) {
            e[i].off = *(int *)vagi;
            if (i > 0) e[i - 1].size = *(int *)vagi - fr.scratch;
            fr.scratch = *(int *)vagi;
        }
        e[i - 1].size = fr.bdSize[k] - fr.scratch;   /* (the BD offset is not reset between the files) */
    }

    /* merge order: ascending id, strictly above the previous one */
    fr.scratch = 0;
    int n = 0;
    if (total != 0) {
        int last = 0;
        mix_order *o = fr.order;
        do {
            int best = 0x7fffffff;
            for (int k = 0; k < 2; k++) {
                mix_entry *e = &fr.tab[0][0] + k * 64;
                for (int i = 0; i < fr.count[k]; i++) {
                    int s = e[i].sss;
                    if (s < best && last < s) {
                        o->file = (short)k;
                        best = s;
                        o->idx = (short)i;
                    }
                }
            }
            if (best == 0x7fffffff) break;
            last = best;
            o++;
            fr.scratch++;
            n++;
        } while ((unsigned int)fr.scratch < (unsigned int)total);
    }

    if (n == 0) {
        CopyMemory(pbufUniHd, fr.ctxp[0]->h_top, fr.hdSize[0]);
        CopyMemory(pbufUniBd, fr.ctxp[0]->b_top, fr.bdSize[0]);
        *psizeUniHd = fr.hdSize[0];
        *psizeUniBd = fr.bdSize[0];
        return;
    }

    /* merged HD: Vers (from the first file), Head, then Vagi / Smpl / Sset / Prog, each a header + n offsets + n
     * entries in merged order (entries patched with their new BD offset / index before the copy) */
    strncpy(fr.head, "IECS", 4);
    strncpy(fr.head + 4, "daeH", 4);
    *(int *)(fr.head + 8) = 0x40;
    *(int *)(fr.head + 0xc) = n * 0x90 + 0x90;
    *(int *)(fr.head + 0x10) = 0;
    *(int *)(fr.head + 0x10) = *(int *)(fr.head + 0x10) + (int)fr.ctxp[0]->sf.sz_BD;
    *(int *)(fr.head + 0x10) = *(int *)(fr.head + 0x10) + (int)fr.ctxp[1]->sf.sz_BD;
    *(int *)(fr.head + 0x20) = 0x50;
    *(int *)(fr.head + 0x1c) = n * 0x10 + 0x60;
    *(int *)(fr.head + 0x18) = n * 0x40 + 0x70;
    *(int *)(fr.head + 0x14) = n * 0x50 + 0x80;
    for (int i = 0; i < 0x1c; i++) fr.head[0x24 + i] = (char)0xff;
    mix_chunk_header(fr.chunk[0], "igaV", n * 0x10 + 0x10, n);
    mix_chunk_header(fr.chunk[1], "lpmS", n * 0x30 + 0x10, n);
    mix_chunk_header(fr.chunk[2], "tesS", n * 0x10 + 0x10, n);
    mix_chunk_header(fr.chunk[3], "gorP", n * 0x40 + 0x10, n);
    CopyMemory(pbufUniHd, fr.ctxp[0]->pVers, 0x10);
    CopyMemory(pbufUniHd + 0x10, fr.head, 0x40);
    CopyMemory(pbufUniHd + 0x50, fr.chunk[0], 0x10);
    char *d = pbufUniHd + 0x60;
    int first = n * 4 + 0x10;

    fr.scratch = first;
    for (int i = 0; i < n; i++, d += 4) {
        CopyMemory(d, &fr.scratch, 4);
        fr.scratch += 0xc;
    }
    fr.scratch = 0;
    for (int i = 0; i < n; i++, d += 0xc) {
        int f = fr.order[i].file, idx = fr.order[i].idx;
        char *src = fr.ctxp[f]->sf.vagi + idx * 0xc;
        *(int *)src = fr.scratch;
        CopyMemory(d, src, 0xc);
        fr.scratch += fr.tab[f][idx].size;
    }

    CopyMemory(d, fr.chunk[1], 0x10);
    fr.scratch = first;
    d += 0x10;
    for (int i = 0; i < n; i++, d += 4) {
        CopyMemory(d, &fr.scratch, 4);
        fr.scratch += 0x2c;
    }
    for (int i = 0; i < n; i++, d += 0x2c) {
        char *src = fr.ctxp[fr.order[i].file]->sf.smpl + fr.order[i].idx * 0x2c;
        *(unsigned short *)src = (unsigned short)i;
        CopyMemory(d, src, 0x2c);
    }

    CopyMemory(d, fr.chunk[2], 0x10);
    fr.scratch = first;
    d += 0x10;
    for (int i = 0; i < n; i++, d += 4) {
        CopyMemory(d, &fr.scratch, 4);
        fr.scratch += 0xc;
    }
    for (int i = 0; i < n; i++, d += 0xc) {
        char *src = fr.ctxp[fr.order[i].file]->sf.sset + fr.order[i].idx * 0xc;
        *(unsigned short *)(src + 4) = (unsigned short)i;
        CopyMemory(d, src, 0xc);
    }

    CopyMemory(d, fr.chunk[3], 0x10);
    fr.scratch = first;
    d += 0x10;
    for (int i = 0; i < n; i++, d += 4) {
        CopyMemory(d, &fr.scratch, 4);
        fr.scratch += 0x3c;
    }
    fr.scratch = 0;
    for (int i = 0; i < n; i++, d += 0x3c) {
        char *src = fr.ctxp[fr.order[i].file]->sf.prog + fr.order[i].idx * 0x3c;
        fr.scratch = *(int *)src;
        *(unsigned short *)(src + fr.scratch) = (unsigned short)i;
        CopyMemory(d, src, 0x3c);
    }

    /* merged BD in the same order */
    char *b = pbufUniBd;
    for (int i = 0; i < n; i++) {
        int f = fr.order[i].file;
        mix_entry *e = &fr.tab[f][fr.order[i].idx];
        CopyMemory(b, fr.ctxp[f]->b_top + e->off, e->size);
        b += e->size;
    }
    *psizeUniHd = (int)(d - pbufUniHd);
    *psizeUniBd = (int)(b - pbufUniBd);
    for (int i = 0; i < n; i++) tblSss[fr.tab[fr.order[i].file][fr.order[i].idx].sss] = (short)i;
}
