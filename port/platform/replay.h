/* Replay files (format v2, P4b): reader, per-vblank playback and the recorder's line writer - one copy in both builds
 * (hybrid runtime/video.c, source port sourceport/main/host_vsync.cpp), so a replay drives both identically. Moved out
 * of runtime/video.c (phase 3 boot slice, input past the title); behaviour frozen to it.
 * Format: line 1 "#acnb-replay 2" (required: a v1 file is refused), line 2 optionally the game-state settings header
 * "#acnb-sim .." (handed to the host's header callback), other '#' lines comments, "seg N begin|end V" segment marks,
 * pad-1 lines "V B LX LY RX RY", pad-2 lines "p2 V C B LX LY RX RY" (C = plugged in; none = port 1 never connected).
 * All hex except V / N / C. Parsing stops at the first line that is none of these (as the old fscanf loop did).
 * Playback: at vblank vb each pad takes the last line with V <= vb (pad 1 starts neutral + connected, pad 2 neutral +
 * unplugged); the replay alone decides both pads and pad 2's plug state. Generic: no ACNB addresses.
 * Format v3: v2 + an "#acnb-sbx .." header line, "#sbx-line" comments
 * (the expanded script) and event lines "x V TEXT" (pokes / freezes, applied at the poll point of vblank V by the
 * sandbox's player, platform/sandbox/events.cpp). The reader keeps the header line and the events as text; a build
 * must hand a v3 replay to sbx_replay_load (which refuses what it cannot play) - an "x" line in a v2 file still stops
 * parsing as before. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "ps2_pad.h"
#ifdef __cplusplus
extern "C" {
#endif

#define REPLAY_MAX_SEGS 64

enum { REPLAY_OK = 1, REPLAY_NO_FILE = 0, REPLAY_NOT_V2 = -1 };

/* Loads F into the run's single replay. hdr(line) is called for line 2 when it starts with '#' (the raw line incl. its
 * EOL) and returns 1 if it was the settings header; *had_hdr = that result (0 = a legacy replay without header).
 * REPLAY_OK makes replay_active() 1 even for a file without pad lines. */
int replay_load(const char *path, int (*hdr)(const char *line), int *had_hdr);
int replay_active(void);
int replay_version(void);              /* 2 / 3 of the loaded replay (0 = none) */
const char *replay_sbx_header(void);   /* v3: the "#acnb-sbx .." line, 0 = none */
int replay_nevents(void);              /* v3 event lines */
/* event i (file order): vblank, text after "x V ", file line; 0 = no such event */
int replay_event(int i, uint32_t *vb, const char **text, int *line);
int replay_nrows(int pad);   /* pad 0 / 1 line counts */
/* pad 2's first line (for the load log); 0 = none */
int replay_p2_first(uint32_t *vb, int *on);
int replay_nsegs(void);      /* highest segment number seen */
/* segment n (1-based): begin / end vblank (0 = absent) */
void replay_seg(int n, uint32_t *begin, uint32_t *end);
/* the frames for vblank vb (call once per vblank, vb non-decreasing) */
void replay_frame(uint32_t vb, PadFrame p[2], int *on2);

/* Recorder lines: "#acnb-replay 2" + "#acnb-sim <simcfg>" (simcfg 0 = no settings line), then per polled vblank a pad-1
 * line when its frame changes (the first call always writes) and a "p2" line when pad 2's plug state / frame changes
 * from (unplugged, neutral). Flushed per line. */
void replay_rec_header(FILE *f, const char *simcfg);
void replay_rec_header_v(FILE *f, int ver, const char *simcfg);   /* "#acnb-replay VER" + settings line */
void replay_rec_frame(FILE *f, uint32_t vb, const PadFrame p[2], int on2);

#ifdef __cplusplus
}
#endif
