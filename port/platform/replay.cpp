/* Replay files (format v2, v3 = sandbox): see replay.h. Was runtime/video.c's --replay / --record code (s93 P4b), moved unchanged. */
#include "replay.h"
#include <stdlib.h>
#include <string.h>

namespace {

struct Row { uint32_t frame; uint16_t b; uint8_t lx, ly, rx, ry, on; };   /* on: pad 2's plugged-in flag (pad 1: 1) */

Row *rep, *rep2;
int nrep, rep_i, nrep2, rep2_i, active, version;
struct Ev { uint32_t vb; int line; char *text; };   /* v3 "x V TEXT" lines (sandbox events), file order */
Ev *evs;
int nevs, cap_evs;
char *sbx_hdr;   /* v3 "#acnb-sbx .." line (EOL stripped) */
struct { uint32_t begin, end; } segs[REPLAY_MAX_SEGS];
int nsegs;

void push(Row *&v, int &n, int &cap, int first, const Row &r)
{
    if (n == cap) { cap = cap ? cap * 2 : first; v = (Row *)realloc(v, cap * sizeof *v); }
    v[n++] = r;
}

}  // namespace

extern "C" int replay_load(const char *path, int (*hdr)(const char *line), int *had_hdr)
{
    FILE *f = fopen(path, "r");
    unsigned fr, bb, a, b2, c2, d, on;
    if (had_hdr) *had_hdr = 0;
    if (!f) return REPLAY_NO_FILE;
    int cap = 0, cap2 = 0, sn, ln = 0, ver = 0;
    char line[4096], kind[8];   /* v3 "#sbx-line" comments carry whole script lines */
    while (fgets(line, sizeof line, f)) {
        ln++;
        if (ln == 1) {   /* format line: required (1-pad v1 files have none) */
            if (sscanf(line, "#acnb-replay %d", &ver) != 1) ver = 0;
            if (ver != 2 && ver != 3) break;
            continue;
        }
        if (line[0] == '#') {   /* line 2: settings header (the host's simcfg); other # lines: comments */
            if (ln == 2 && hdr && hdr(line) && had_hdr) *had_hdr = 1;
            if (ver == 3 && !sbx_hdr && !strncmp(line, "#acnb-sbx ", 10)) {
                line[strcspn(line, "\r\n")] = 0;
                sbx_hdr = strdup(line);
            }
            continue;
        }
        if (ver == 3 && line[0] == 'x' && line[1] == ' ') {   /* sandbox event: "x V TEXT", kept as text for the sandbox */
            char *e;
            unsigned long v = strtoul(line + 2, &e, 10);
            if (e == line + 2 || *e != ' ') break;
            e[strcspn(e, "\r\n")] = 0;
            if (nevs == cap_evs) evs = (Ev *)realloc(evs, (size_t)(cap_evs = cap_evs ? cap_evs * 2 : 64) * sizeof *evs);
            evs[nevs++] = Ev{(uint32_t)v, ln, strdup(e + 1)};
            continue;
        }
        if (sscanf(line, "seg %d %7s %u", &sn, kind, &fr) == 3) {
            if (sn >= 1 && sn <= REPLAY_MAX_SEGS) {
                if (!strcmp(kind, "begin")) segs[sn - 1].begin = fr; else if (!strcmp(kind, "end")) segs[sn - 1].end = fr;
                if (sn > nsegs) nsegs = sn;
            }
            continue;
        }
        if (!strncmp(line, "p2 ", 3)) {
            if (sscanf(line + 3, "%u %u %x %x %x %x %x", &fr, &on, &bb, &a, &b2, &c2, &d) != 7) break;
            push(rep2, nrep2, cap2, 64, Row{fr, (uint16_t)bb, (uint8_t)a, (uint8_t)b2, (uint8_t)c2, (uint8_t)d, (uint8_t)(on != 0)});
            continue;
        }
        if (sscanf(line, "%u %x %x %x %x %x", &fr, &bb, &a, &b2, &c2, &d) != 6) break;   /* stops where the old fscanf loop did */
        push(rep, nrep, cap, 256, Row{fr, (uint16_t)bb, (uint8_t)a, (uint8_t)b2, (uint8_t)c2, (uint8_t)d, 1});
    }
    fclose(f);
    if (ver != 2 && ver != 3) return REPLAY_NOT_V2;
    active = 1;
    version = ver;
    return REPLAY_OK;
}

extern "C" int replay_active(void) { return active; }
extern "C" int replay_version(void) { return version; }
extern "C" const char *replay_sbx_header(void) { return sbx_hdr; }
extern "C" int replay_nevents(void) { return nevs; }
extern "C" int replay_event(int i, uint32_t *vb, const char **text, int *line)
{
    if (i < 0 || i >= nevs) return 0;
    *vb = evs[i].vb; *text = evs[i].text;
    if (line) *line = evs[i].line;
    return 1;
}
extern "C" int replay_nrows(int pad) { return pad ? nrep2 : nrep; }
extern "C" int replay_p2_first(uint32_t *vb, int *on)
{
    if (!nrep2) return 0;
    *vb = rep2[0].frame; *on = rep2[0].on;
    return 1;
}
extern "C" int replay_nsegs(void) { return nsegs; }
extern "C" void replay_seg(int n, uint32_t *begin, uint32_t *end)
{
    *begin = *end = 0;
    if (n >= 1 && n <= REPLAY_MAX_SEGS) { *begin = segs[n - 1].begin; *end = segs[n - 1].end; }
}

extern "C" void replay_frame(uint32_t vb, PadFrame p[2], int *on2)
{
    static Row r1 = {0, 0, 0x80, 0x80, 0x80, 0x80, 1}, r2 = {0, 0, 0x80, 0x80, 0x80, 0x80, 0};
    while (rep_i < nrep && rep[rep_i].frame <= vb) r1 = rep[rep_i++];
    while (rep2_i < nrep2 && rep2[rep2_i].frame <= vb) r2 = rep2[rep2_i++];
    p[0].buttons = r1.b; p[0].lx = r1.lx; p[0].ly = r1.ly; p[0].rx = r1.rx; p[0].ry = r1.ry;
    p[1].buttons = r2.b; p[1].lx = r2.lx; p[1].ly = r2.ly; p[1].rx = r2.rx; p[1].ry = r2.ry;
    *on2 = r2.on;
}

extern "C" void replay_rec_header(FILE *f, const char *simcfg) { replay_rec_header_v(f, 2, simcfg); }
extern "C" void replay_rec_header_v(FILE *f, int ver, const char *simcfg)
{
    fprintf(f, "#acnb-replay %d\n", ver);
    if (simcfg) fprintf(f, "#acnb-sim %s\n", simcfg);
    fflush(f);
}

extern "C" void replay_rec_frame(FILE *f, uint32_t vb, const PadFrame p[2], int on2)
{
    static uint32_t last = 0xffffffffu, last2;   /* pad 1: the first poll always logs */
    uint32_t cur = p[0].buttons | (uint32_t)p[0].lx << 16 | (uint32_t)p[0].ly << 24, cur2 = p[0].rx | p[0].ry << 8;
    if (cur != last || cur2 != last2) {
        fprintf(f, "%u %04x %02x %02x %02x %02x\n", vb, p[0].buttons, p[0].lx, p[0].ly, p[0].rx, p[0].ry);
        fflush(f);
        last = cur; last2 = cur2;
    }
    static int pon;   /* pad 2: from (unplugged, neutral), so a 1-pad recording has no p2 line */
    static PadFrame plast = {0, 0x80, 0x80, 0x80, 0x80};
    if (on2 != pon || memcmp(&p[1], &plast, sizeof plast)) {
        fprintf(f, "p2 %u %d %04x %02x %02x %02x %02x\n", vb, on2, p[1].buttons, p[1].lx, p[1].ly, p[1].rx, p[1].ry);
        fflush(f);
        pon = on2; plast = p[1];
    }
}
